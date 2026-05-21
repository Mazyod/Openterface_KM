#!/usr/bin/env python3
"""Switch Openterface USB work mode.

Tries two methods in order:
1. HID Feature Report 0x30 on target-facing USBFS (PID 0xFE00, IF 3)
2. CH9329-over-CDC bulk transfer on host-facing USBD (PID 0xFE0C) or target USBFS

Requires: pyusb (pip install pyusb)
On macOS: sudo python3 tools/usb_mode_switch.py

Usage:
    sudo python3 tools/usb_mode_switch.py                     # Read current mode
    sudo python3 tools/usb_mode_switch.py --mode 4 --reboot   # Switch to ECM + reboot
    sudo python3 tools/usb_mode_switch.py --mode 2 --reboot   # Switch back to default
    sudo python3 tools/usb_mode_switch.py --clear             # Clear stored mode
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import usb.core
    import usb.util
except ImportError:
    sys.exit("pyusb is required.  pip install pyusb")


VID = 0x1A86
HOST_PID = 0xFE0C   # USBD (host-facing)
TARGET_PID = 0xFE00  # USBFS (target-facing)

MODE_NAMES = {
    0: "unknown",
    1: "HID + CDC ACM",
    2: "HID + CDC ACM + UAC (default)",
    3: "HID + CDC ACM Bridge",
    4: "HID + CDC ECM Bridge",
}
STATUS_LABELS = {0: "OK", 1: "ERR_PARAM", 2: "ERR_FRAME"}

CH9329_HEAD1 = 0x57
CH9329_HEAD2 = 0xAB
CH9329_CMD_USB_WORK_MODE = 0x30


# ---------------------------------------------------------------------------
# Method 1: HID Feature Report 0x30 (target-facing USBFS, IF 3 Media Key)
# ---------------------------------------------------------------------------

def _hid_set_report(dev, data: bytes, timeout: int = 3000) -> bool:
    try:
        dev.ctrl_transfer(
            bmRequestType=0x21,  # H2D, Class, Interface
            bRequest=0x09,       # SET_REPORT
            wValue=(3 << 8) | 0x30,  # Feature, ID 0x30
            wIndex=0x03,
            data_or_wLength=data,
            timeout=timeout,
        )
        return True
    except usb.core.USBError as e:
        print(f"  HID SET_REPORT failed: {e}")
        return False


def _hid_get_report(dev, timeout: int = 3000) -> bytes | None:
    try:
        buf = dev.ctrl_transfer(
            bmRequestType=0xA1,  # D2H, Class, Interface
            bRequest=0x01,       # GET_REPORT
            wValue=(3 << 8) | 0x30,
            wIndex=0x03,
            data_or_wLength=64,
            timeout=timeout,
        )
        return bytes(buf)
    except usb.core.USBError as e:
        print(f"  HID GET_REPORT failed: {e}")
        return None


def try_hid_method(dev, cmd: int, payload: bytes = b"", timeout: int = 3000):
    """Try HID Feature Report method. Returns decoded dict or None."""
    data = bytes([0x30, cmd]) + payload
    if not _hid_set_report(dev, data, timeout):
        return None
    time.sleep(0.1)
    report = _hid_get_report(dev, timeout)
    if report is None or len(report) < 12 or report[0] != 0x30:
        return None
    return {
        "method": "HID",
        "last_status": report[1],
        "compiled_mode": report[2],
        "active_mode": report[3],
        "stored_mode": report[4],
        "config_status": report[5],
        "flags": report[6] | (report[7] << 8),
        "capability": report[8] | (report[9] << 8) | (report[10] << 16) | (report[11] << 24),
    }


# ---------------------------------------------------------------------------
# Method 2: CH9329-over-CDC bulk transfer
# ---------------------------------------------------------------------------

def _find_cdc_data_ep(dev):
    """Find bulk OUT/IN on any CDC Data (class 10) interface."""
    for cfg in dev:
        for intf in cfg:
            if intf.bInterfaceClass != 10:
                continue
            ep_out = None
            ep_in = None
            for ep in intf:
                if (ep.bmAttributes & 0x03) != 2:
                    continue
                if ep.bEndpointAddress & 0x80:
                    ep_in = ep
                else:
                    ep_out = ep
            if ep_out:
                return intf, ep_out, ep_in
    return None, None, None


def _ch9329_frame(addr: int, cmd: int, payload: bytes = b"") -> bytes:
    frame = bytes([CH9329_HEAD1, CH9329_HEAD2, addr, cmd, len(payload)])
    frame += payload
    frame += bytes([sum(frame) & 0xFF])
    return frame


def try_ch9329_method(dev, cmd: int, payload: bytes = b"", timeout: int = 3000,
                      verbose: bool = False):
    """Try CH9329-over-CDC bulk method. Returns decoded dict or None."""
    data_if, ep_out, ep_in = _find_cdc_data_ep(dev)
    if data_if is None or ep_out is None:
        return None

    if_num = data_if.bInterfaceNumber
    try:
        if dev.is_kernel_driver_active(if_num):
            dev.detach_kernel_driver(if_num)
    except (NotImplementedError, usb.core.USBError):
        pass

    try:
        dev.set_configuration()
    except usb.core.USBError:
        pass

    try:
        usb.util.claim_interface(dev, if_num)
    except usb.core.USBError:
        return None

    try:
        frame = _ch9329_frame(0x00, CH9329_CMD_USB_WORK_MODE, bytes([cmd]) + payload)
        if verbose:
            print(f"  TX: {frame.hex(' ')}")
        ep_out.write(frame, timeout=timeout)
        time.sleep(0.1)

        if ep_in:
            try:
                raw = bytes(ep_in.read(256, timeout=timeout))
                if verbose:
                    print(f"  RX: {raw.hex(' ')}")
                # Find CH9329 header
                offset = 0
                while offset < len(raw) - 1:
                    if raw[offset] == CH9329_HEAD1 and raw[offset + 1] == CH9329_HEAD2:
                        break
                    offset += 1
                if offset >= len(raw) - 1:
                    return None
                raw = raw[offset:]
                plen = raw[4]
                if len(raw) < 6 + plen:
                    return None
                p = raw[5:5 + plen]
                if len(p) >= 12:
                    return {
                        "method": "CH9329",
                        "last_status": p[0],
                        "compiled_mode": p[1],
                        "active_mode": p[2],
                        "stored_mode": p[3],
                        "config_status": p[4],
                        "flags": p[5] | (p[6] << 8),
                        "capability": p[7] | (p[8] << 8) | (p[9] << 16) | (p[10] << 24),
                    }
            except usb.core.USBTimeoutError:
                pass
    finally:
        usb.util.release_interface(dev, if_num)

    return None


# ---------------------------------------------------------------------------
# Shared helpers
# ---------------------------------------------------------------------------

def _print_status(info: dict):
    method = info.get("method", "?")
    print(f"  Method: {method}")
    print(f"  last_status:   {STATUS_LABELS.get(info.get('last_status', -1), '?')}")
    print(f"  compiled_mode: {info.get('compiled_mode', '?')} ({MODE_NAMES.get(info.get('compiled_mode', 0), '?')})")
    print(f"  active_mode:   {info.get('active_mode', '?')} ({MODE_NAMES.get(info.get('active_mode', 0), '?')})")
    print(f"  stored_mode:   {info.get('stored_mode', '?')} ({MODE_NAMES.get(info.get('stored_mode', 0), '?')})")

    cs = info.get("config_status", 0)
    st = []
    if cs & 0x01:
        st.append("VALID")
    if cs & 0x02:
        st.append("MISMATCH")
    print(f"  config_status: {cs} ({', '.join(st) if st else 'none'})")
    print(f"  flags:         0x{info.get('flags', 0):04X}")

    cap = info.get("capability", 0)
    supported = [str(m) for m in range(1, 5) if cap & (1 << (m - 1))]
    print(f"  capability:    0x{cap:08X} (modes: {', '.join(supported) if supported else 'none'})")


def _read(dev, verbose: bool = False) -> dict | None:
    # Try HID first, then CH9329
    info = try_hid_method(dev, cmd=0x00)
    if info is not None:
        _print_status(info)
        return info
    info = try_ch9329_method(dev, cmd=0x00, verbose=verbose)
    if info is not None:
        _print_status(info)
        return info
    print("  Both HID and CH9329 methods failed.")
    return None


def _write(dev, mode: int, flags: int = 0, verbose: bool = False) -> bool:
    payload = bytes([mode & 0xFF, flags & 0xFF, (flags >> 8) & 0xFF])
    info = try_hid_method(dev, cmd=0x01, payload=payload)
    if info is None:
        info = try_ch9329_method(dev, cmd=0x01, payload=bytes([mode & 0xFF]) + payload[1:], verbose=verbose)
    if info is not None:
        _print_status(info)
        return info.get("last_status") == 0
    print("  Write failed: no response.")
    return False


def _clear(dev, verbose: bool = False) -> bool:
    info = try_hid_method(dev, cmd=0x02)
    if info is None:
        info = try_ch9329_method(dev, cmd=0x02, verbose=verbose)
    if info is not None:
        _print_status(info)
        return info.get("last_status") == 0
    return False


def _reboot(dev, verbose: bool = False):
    try_hid_method(dev, cmd=0x03)
    try_ch9329_method(dev, cmd=0x03, verbose=verbose)
    print("  Reboot requested. Device will disconnect and reconnect.")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Switch Openterface USB work mode.")
    parser.add_argument("--mode", type=int, choices=[1, 2, 3, 4],
                        help="Set USB work mode")
    parser.add_argument("--flags", type=lambda v: int(v, 0), default=0,
                        help="Flags for mode write (default 0)")
    parser.add_argument("--clear", action="store_true", help="Clear stored mode")
    parser.add_argument("--reboot", action="store_true", help="Reboot after write")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    # Find device: try host-facing first, then target-facing
    dev = usb.core.find(idVendor=VID, idProduct=HOST_PID)
    source = "host-facing (USBD)"
    if dev is None:
        dev = usb.core.find(idVendor=VID, idProduct=TARGET_PID)
        source = "target-facing (USBFS)"
    if dev is None:
        sys.exit(f"Device not found (VID=0x{VID:04X}, PID=0x{HOST_PID:04X}/0x{TARGET_PID:04X}).")

    print(f"Device: {dev.manufacturer} {dev.product} (PID=0x{dev.idProduct:04X}, {source})")

    print("\nCurrent status:")
    info = _read(dev, verbose=args.verbose)
    if info is None:
        return 1

    if args.mode is not None:
        print(f"\nWriting mode {args.mode} ({MODE_NAMES.get(args.mode, '?')})...")
        if _write(dev, args.mode, args.flags, verbose=args.verbose):
            if args.reboot:
                _reboot(dev)
        else:
            return 1
    elif args.clear:
        print("\nClearing stored mode...")
        _clear(dev)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
