#!/usr/bin/env python3
import sys
if sys.platform != "win32":
    import pytest
    pytest.skip("Windows-only test", allow_module_level=True)

import argparse
import ctypes
from ctypes import wintypes


DIGCF_PRESENT = 0x00000002
DIGCF_DEVICEINTERFACE = 0x00000010
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 0x00000001
FILE_SHARE_WRITE = 0x00000002
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
REPORT_ID = 0x30
REPORT_LEN = 64


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", wintypes.DWORD),
        ("Data2", wintypes.WORD),
        ("Data3", wintypes.WORD),
        ("Data4", ctypes.c_ubyte * 8),
    ]


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD),
        ("InterfaceClassGuid", GUID),
        ("Flags", wintypes.DWORD),
        ("Reserved", ctypes.c_size_t),
    ]


class HIDD_ATTRIBUTES(ctypes.Structure):
    _fields_ = [
        ("Size", wintypes.ULONG),
        ("VendorID", wintypes.USHORT),
        ("ProductID", wintypes.USHORT),
        ("VersionNumber", wintypes.USHORT),
    ]


setupapi = ctypes.WinDLL("setupapi", use_last_error=True)
hid = ctypes.WinDLL("hid", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

hid.HidD_GetHidGuid.argtypes = [ctypes.POINTER(GUID)]
hid.HidD_GetAttributes.argtypes = [wintypes.HANDLE, ctypes.POINTER(HIDD_ATTRIBUTES)]
hid.HidD_GetAttributes.restype = wintypes.BOOLEAN
hid.HidD_GetFeature.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.ULONG]
hid.HidD_GetFeature.restype = wintypes.BOOLEAN
hid.HidD_SetFeature.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.ULONG]
hid.HidD_SetFeature.restype = wintypes.BOOLEAN

setupapi.SetupDiGetClassDevsW.argtypes = [ctypes.POINTER(GUID), wintypes.LPCWSTR, wintypes.HWND, wintypes.DWORD]
setupapi.SetupDiGetClassDevsW.restype = wintypes.HANDLE
setupapi.SetupDiEnumDeviceInterfaces.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(GUID), wintypes.DWORD, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA)]
setupapi.SetupDiEnumDeviceInterfaces.restype = wintypes.BOOL
setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [wintypes.HANDLE, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA), ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
setupapi.SetupDiGetDeviceInterfaceDetailW.restype = wintypes.BOOL
setupapi.SetupDiDestroyDeviceInfoList.argtypes = [wintypes.HANDLE]
setupapi.SetupDiDestroyDeviceInfoList.restype = wintypes.BOOL

kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL


def iter_hid_paths():
    guid = GUID()
    hid.HidD_GetHidGuid(ctypes.byref(guid))
    info = setupapi.SetupDiGetClassDevsW(ctypes.byref(guid), None, None, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
    if info == INVALID_HANDLE_VALUE:
        raise ctypes.WinError(ctypes.get_last_error())

    try:
        index = 0
        while True:
            iface = SP_DEVICE_INTERFACE_DATA()
            iface.cbSize = ctypes.sizeof(iface)
            if not setupapi.SetupDiEnumDeviceInterfaces(info, None, ctypes.byref(guid), index, ctypes.byref(iface)):
                break

            required = wintypes.DWORD(0)
            setupapi.SetupDiGetDeviceInterfaceDetailW(info, ctypes.byref(iface), None, 0, ctypes.byref(required), None)
            detail = ctypes.create_string_buffer(required.value)
            ctypes.c_ulong.from_buffer(detail).value = 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6
            if setupapi.SetupDiGetDeviceInterfaceDetailW(info, ctypes.byref(iface), detail, required, ctypes.byref(required), None):
                yield ctypes.wstring_at(ctypes.addressof(detail) + ctypes.sizeof(wintypes.DWORD))
            index += 1
    finally:
        setupapi.SetupDiDestroyDeviceInfoList(info)


def open_hid(path):
    return kernel32.CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, None, OPEN_EXISTING, 0, None)


def get_attrs(handle):
    attrs = HIDD_ATTRIBUTES()
    attrs.Size = ctypes.sizeof(attrs)
    if not hid.HidD_GetAttributes(handle, ctypes.byref(attrs)):
        raise ctypes.WinError(ctypes.get_last_error())
    return attrs


def get_feature(handle):
    report = (ctypes.c_ubyte * REPORT_LEN)()
    report[0] = REPORT_ID
    if not hid.HidD_GetFeature(handle, report, REPORT_LEN):
        return None
    return bytes(report)


def set_feature(handle, op, mode=0, flags=0):
    report = (ctypes.c_ubyte * REPORT_LEN)()
    report[0] = REPORT_ID
    report[1] = op
    report[2] = mode & 0xFF
    report[3] = flags & 0xFF
    report[4] = (flags >> 8) & 0xFF
    if not hid.HidD_SetFeature(handle, report, REPORT_LEN):
        raise ctypes.WinError(ctypes.get_last_error())


def decode_report(report):
    capability = report[8] | (report[9] << 8) | (report[10] << 16) | (report[11] << 24)
    flags = report[6] | (report[7] << 8)
    return {
        "last_status": report[1],
        "compiled_mode": report[2],
        "active_mode": report[3],
        "stored_mode": report[4],
        "config_status": report[5],
        "flags": flags,
        "capability_mask": capability,
    }


def main():
    parser = argparse.ArgumentParser(description="Read/write Openterface USB mode config over vendor HID feature report 0x30.")
    parser.add_argument("--vid", type=lambda value: int(value, 0), default=0x1A86)
    parser.add_argument("--pid", type=lambda value: int(value, 0), default=0xFE00)
    parser.add_argument("--write-mode", type=int, choices=[1, 2, 3, 4])
    parser.add_argument("--flags", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--clear", action="store_true")
    parser.add_argument("--reboot", action="store_true")
    args = parser.parse_args()

    found = []
    for path in iter_hid_paths():
        handle = open_hid(path)
        if handle == INVALID_HANDLE_VALUE:
            continue
        try:
            attrs = get_attrs(handle)
            if attrs.VendorID != args.vid or attrs.ProductID != args.pid:
                continue
            report = get_feature(handle)
            if report is None or report[0] != REPORT_ID:
                continue

            found.append(path)
            if args.clear:
                set_feature(handle, 0x02)
            if args.write_mode is not None:
                set_feature(handle, 0x01, args.write_mode, args.flags)
            if args.reboot:
                set_feature(handle, 0x03)

            report = get_feature(handle)
            if report is not None:
                data = decode_report(report)
                print(f"Device: {path}")
                for key, value in data.items():
                    if key == "capability_mask":
                        print(f"  {key}: 0x{value:08X}")
                    else:
                        print(f"  {key}: {value}")
        finally:
            kernel32.CloseHandle(handle)

    if not found:
        print("No Openterface USB mode HID feature report found.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())