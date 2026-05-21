#!/usr/bin/env python3
"""Switch Openterface USB work mode.

Two methods are auto-selected based on which device is connected:

1. HID Feature Report 0x30 -- target-facing USBFS (PID 0xFE00, IF 3).
   Works without sudo on macOS via libusb auto-detach.

2. CH9329-over-CDC-ACM bulk -- host-facing USBD (PID 0xFE0C, IF 1).
   Requires sudo on macOS to detach the kernel CDC ACM driver.

Usage:
    # Target-facing USBFS (PID 0xFE00) -- no sudo needed
    python3 tools/usb_mode_switch_mac.py
    python3 tools/usb_mode_switch_mac.py --mode 4 --reboot

    # Host-facing USBD (PID 0xFE0C) -- sudo required on macOS
    sudo python3 tools/usb_mode_switch_mac.py
    sudo python3 tools/usb_mode_switch_mac.py --mode 4 --reboot
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util as cutil
import os.path
import sys
import time

VID = 0x1A86
HOST_PID = 0xFE0C    # USBD (host-facing)
TARGET_PID = 0xFE00  # USBFS (target-facing)

MODE_NAMES = {
    1: "HID + CDC ACM",
    2: "HID + CDC ACM + UAC (default)",
    3: "HID + CDC ACM Bridge",
    4: "HID + CDC ECM Bridge",
}
STATUS_LABELS = {0: "OK", 1: "ERR_PARAM", 2: "ERR_FRAME"}

# ---------------------------------------------------------------------------
# libusb setup
# ---------------------------------------------------------------------------

def _load_libusb():
    for candidate in [
        "/opt/homebrew/lib/libusb-1.0.dylib",
        "/usr/local/lib/libusb-1.0.dylib",
        "/usr/lib/libusb-1.0.dylib",
    ]:
        if os.path.exists(candidate):
            return ctypes.CDLL(candidate)
    name = cutil.find_library("usb-1.0") or cutil.find_library("usb") or "libusb-1.0.so.0"
    return ctypes.CDLL(name)


def _init_libusb(lib):
    ctx = ctypes.c_void_p()
    ret = lib.libusb_init(ctypes.byref(ctx))
    if ret != 0:
        sys.exit(f"libusb_init failed: {ret}")
    return ctx


def _setup_prototypes(lib):
    lib.libusb_init.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    lib.libusb_init.restype = ctypes.c_int
    lib.libusb_exit.argtypes = [ctypes.c_void_p]
    lib.libusb_exit.restype = None
    lib.libusb_get_device_list.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    lib.libusb_get_device_list.restype = ctypes.c_ssize_t
    lib.libusb_free_device_list.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.libusb_free_device_list.restype = None
    lib.libusb_ref_device.argtypes = [ctypes.c_void_p]
    lib.libusb_ref_device.restype = None
    lib.libusb_get_device_descriptor.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    lib.libusb_get_device_descriptor.restype = ctypes.c_int
    lib.libusb_get_config_descriptor.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, ctypes.POINTER(ctypes.c_void_p)]
    lib.libusb_get_config_descriptor.restype = ctypes.c_int
    lib.libusb_free_config_descriptor.argtypes = [ctypes.c_void_p]
    lib.libusb_free_config_descriptor.restype = None
    lib.libusb_open.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    lib.libusb_open.restype = ctypes.c_int
    lib.libusb_close.argtypes = [ctypes.c_void_p]
    lib.libusb_close.restype = None
    lib.libusb_set_configuration.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.libusb_set_configuration.restype = ctypes.c_int
    lib.libusb_claim_interface.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.libusb_claim_interface.restype = ctypes.c_int
    lib.libusb_release_interface.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.libusb_release_interface.restype = ctypes.c_int
    lib.libusb_set_auto_detach_kernel_driver.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.libusb_set_auto_detach_kernel_driver.restype = ctypes.c_int
    lib.libusb_bulk_transfer.argtypes = [
        ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_char_p,
        ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.c_uint,
    ]
    lib.libusb_bulk_transfer.restype = ctypes.c_int
    lib.libusb_control_transfer.argtypes = [
        ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_ubyte,
        ctypes.c_ushort, ctypes.c_ushort, ctypes.c_char_p,
        ctypes.c_ushort, ctypes.c_uint,
    ]
    lib.libusb_control_transfer.restype = ctypes.c_int


def _find_device(lib, ctx, vid, pid):
    dev_list = ctypes.c_void_p()
    n = lib.libusb_get_device_list(ctx, ctypes.byref(dev_list))
    if n < 0:
        sys.exit(f"libusb_get_device_list failed: {n}")

    desc_type = ctypes.c_ubyte * 18
    for i in range(n):
        ptr_array = ctypes.cast(dev_list, ctypes.POINTER(ctypes.c_void_p))
        dev = ctypes.c_void_p(ptr_array[i])
        desc = desc_type()
        if lib.libusb_get_device_descriptor(dev, ctypes.byref(desc)) != 0:
            continue
        dvid = desc[8] | (desc[9] << 8)
        dpid = desc[10] | (desc[11] << 8)
        if dvid == vid and dpid == pid:
            lib.libusb_ref_device(dev)
            lib.libusb_free_device_list(dev_list, 0)
            return dev

    lib.libusb_free_device_list(dev_list, 0)
    return None


def _find_bulk_endpoints(lib, dev):
    """Return CDC Data bulk endpoint addresses.

    The firmware descriptor is fixed, so we use known values rather than
    parsing the libusb config structure (which is pointer-based, not
    contiguous bytes).
    """
    return 0x05, 0x83  # EP5 OUT, EP83 IN


def _print_status(payload: bytes):
    if len(payload) < 12:
        print(f"  Short response ({len(payload)} bytes): {payload.hex(' ')}")
        return

    last_status = payload[0]
    compiled_mode = payload[1]
    active_mode = payload[2]
    stored_mode = payload[3]
    config_status = payload[4]
    flags = payload[5] | (payload[6] << 8)
    capability = payload[7] | (payload[8] << 8) | (payload[9] << 16) | (payload[10] << 24)

    print(f"  last_status:   {STATUS_LABELS.get(last_status, f'0x{last_status:02X}')}")
    print(f"  compiled_mode: {compiled_mode} ({MODE_NAMES.get(compiled_mode, '?')})")
    print(f"  active_mode:   {active_mode} ({MODE_NAMES.get(active_mode, '?')})")
    print(f"  stored_mode:   {stored_mode} ({MODE_NAMES.get(stored_mode, '?')})")

    st_parts = []
    if config_status & 0x01:
        st_parts.append("VALID")
    if config_status & 0x02:
        st_parts.append("MISMATCH")
    print(f"  config_status: {config_status} ({', '.join(st_parts) if st_parts else 'none'})")
    print(f"  flags:         0x{flags:04X}")

    supported = [str(m) for m in range(1, 5) if capability & (1 << m)]
    print(f"  capability:    0x{capability:08X} (modes: {', '.join(supported) if supported else 'none'})")


# ---------------------------------------------------------------------------
# Method A: HID Feature Report (target-facing, PID 0xFE00, IF 3)
# ---------------------------------------------------------------------------

def _hid_send(lib, handle, data_out: bytes, timeout: int = 3000) -> bytes | None:
    """SET_REPORT then GET_REPORT on HID IF 3, Feature ID 0x30."""
    buf = ctypes.create_string_buffer(data_out)
    ret = lib.libusb_control_transfer(
        handle, 0x21, 0x09, (3 << 8) | 0x30, 0x03,
        buf, len(buf), timeout,
    )
    if ret < 0:
        return None
    time.sleep(0.1)

    buf = ctypes.create_string_buffer(64)
    ret = lib.libusb_control_transfer(
        handle, 0xA1, 0x01, (3 << 8) | 0x30, 0x03,
        buf, len(buf), timeout,
    )
    if ret < 12:
        return None
    return bytes(buf[1:ret])  # skip report ID byte


# ---------------------------------------------------------------------------
# Method B: CH9329-over-CDC-ACM bulk (host-facing, PID 0xFE0C, IF 1)
# ---------------------------------------------------------------------------

def _ch9329_frame(subcmd: int, payload: bytes = b"") -> bytes:
    frame = bytes([0x57, 0xAB, 0x00, 0x30, 1 + len(payload), subcmd])
    frame += payload
    frame += bytes([sum(frame) & 0xFF])
    return frame


def _decode_ch9329(raw: bytes) -> bytes | None:
    """Find CH9329 header in raw USB data, extract payload."""
    offset = 0
    while offset < len(raw) - 1:
        if raw[offset] == 0x57 and raw[offset + 1] == 0xAB:
            break
        offset += 1
    if offset >= len(raw) - 1:
        return None
    raw = raw[offset:]
    if len(raw) < 6:
        return None
    plen = raw[4]
    if len(raw) < 6 + plen:
        return None
    return raw[5:5 + plen]


def _acm_send(lib, handle, subcmd: int, payload: bytes = b"",
              ep_out: int = 0x05, ep_in: int = 0x83) -> bytes | None:
    """Send CH9329 frame via bulk OUT, read response from bulk IN."""
    frame = _ch9329_frame(subcmd, payload)
    buf = ctypes.create_string_buffer(frame)
    transferred = ctypes.c_int()

    ret = lib.libusb_bulk_transfer(handle, ep_out, buf, len(frame),
                                   ctypes.byref(transferred), 3000)
    if ret != 0:
        return None

    time.sleep(0.2)

    resp_buf = ctypes.create_string_buffer(256)
    ret = lib.libusb_bulk_transfer(handle, ep_in, resp_buf, len(resp_buf),
                                   ctypes.byref(transferred), 3000)
    if ret != 0 or transferred.value < 6:
        return None

    return _decode_ch9329(bytes(resp_buf[:transferred.value]))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Switch Openterface USB work mode.")
    parser.add_argument("--mode", type=int, choices=[1, 2, 3, 4], help="Set USB work mode")
    parser.add_argument("--flags", type=lambda v: int(v, 0), default=0, help="Flags (default 0)")
    parser.add_argument("--clear", action="store_true", help="Clear stored mode")
    parser.add_argument("--reboot", action="store_true", help="Reboot after write")
    args = parser.parse_args()

    lib = _load_libusb()
    ctx = _init_libusb(lib)
    _setup_prototypes(lib)

    try:
        # Try target-facing first (HID, no sudo), then host-facing (ACM, needs sudo)
        dev = None
        pid = None
        label = None
        method = None  # "hid" or "acm"
        for p, lbl, m in [(TARGET_PID, "target-facing USBFS", "hid"),
                          (HOST_PID, "host-facing USBD", "acm")]:
            dev = _find_device(lib, ctx, VID, p)
            if dev is not None:
                pid = p
                label = lbl
                method = m
                break

        if dev is None:
            sys.exit(f"Device not found (VID=0x{VID:04X}).")

        print(f"Found device at PID 0x{pid:04X} ({label}) -- method: {method}")

        handle = ctypes.c_void_p()
        ret = lib.libusb_open(dev, ctypes.byref(handle))
        if ret != 0:
            sys.exit(f"libusb_open failed: {ret}")

        lib.libusb_set_auto_detach_kernel_driver(handle, 1)
        lib.libusb_set_configuration(handle, 1)
        time.sleep(0.1)

        ep_out = ep_in = None
        if method == "hid":
            ret = lib.libusb_claim_interface(handle, 3)
            if ret != 0:
                print(f"  claim_interface(3) returned {ret}, continuing anyway")
        else:
            ep_out, ep_in = _find_bulk_endpoints(lib, dev)
            if ep_out is None:
                print("  No CDC bulk endpoints found.")
                return 1
            print(f"  CDC bulk: EP_OUT=0x{ep_out:02X} EP_IN=0x{ep_in:02X}")
            ret = lib.libusb_claim_interface(handle, 1)
            if ret != 0:
                print(f"  claim_interface(1) returned {ret} -- try running with sudo")

        try:
            def _read():
                if method == "hid":
                    return _hid_send(lib, handle, bytes([0x30, 0x00]))
                return _acm_send(lib, handle, 0x00, ep_out=ep_out, ep_in=ep_in)

            def _write(mode: int, flags: int = 0):
                if method == "hid":
                    return _hid_send(lib, handle, bytes([0x30, 0x01, mode & 0xFF, flags & 0xFF, (flags >> 8) & 0xFF]))
                return _acm_send(lib, handle, 0x01, bytes([mode & 0xFF, flags & 0xFF, (flags >> 8) & 0xFF]),
                                 ep_out=ep_out, ep_in=ep_in)

            def _clear():
                if method == "hid":
                    return _hid_send(lib, handle, bytes([0x30, 0x02]))
                return _acm_send(lib, handle, 0x02, ep_out=ep_out, ep_in=ep_in)

            def _reboot_cmd():
                if method == "hid":
                    _hid_send(lib, handle, bytes([0x30, 0x03]))
                else:
                    _acm_send(lib, handle, 0x03, ep_out=ep_out, ep_in=ep_in)
                print("  Reboot sent. Device will disconnect and reconnect.")

            print("\nCurrent status:")
            resp = _read()
            if resp is None:
                print("  Failed to read mode.")
                if method == "acm":
                    print("  On host-facing USB (PID 0xFE0C), try: sudo python3 " + sys.argv[0])
                return 1
            _print_status(resp)

            if args.mode is not None:
                print(f"\nWriting mode {args.mode} ({MODE_NAMES.get(args.mode, '?')})...")
                resp = _write(args.mode, args.flags)
                if resp is None:
                    print("  Write failed: no response.")
                    return 1
                _print_status(resp)
                if resp[0] != 0:
                    print(f"  Write error: {STATUS_LABELS.get(resp[0], 'UNKNOWN')}")
                    return 1
                if args.reboot:
                    _reboot_cmd()

            elif args.clear:
                print("\nClearing stored mode...")
                resp = _clear()
                if resp is not None:
                    _print_status(resp)
                else:
                    print("  Clear failed: no response.")

        finally:
            if method == "hid":
                lib.libusb_release_interface(handle, 3)
            else:
                lib.libusb_release_interface(handle, 1)
            lib.libusb_close(handle)

    finally:
        lib.libusb_exit(ctx)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
