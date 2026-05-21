#!/usr/bin/env python3
"""Switch Openterface USB work mode via serial port (/dev/cu.*).

This sends CH9329 protocol frames through the CDC ACM serial device.
The firmware intercepts CH9329 frames on the CDC bulk OUT endpoint,
processes them, and sends the response back on the bulk IN endpoint.

NOTE: On some macOS configurations, the kernel CDC ACM driver may not
properly forward the device's CH9329 responses back to the serial port.
If this script gets no response, use the libusb-based method instead:
    python3 tools/usb_mode_switch_mac.py          (target-facing, no sudo)
    sudo python3 tools/usb_mode_switch_mac.py     (host-facing, needs sudo)

Usage:
    python3 tools/usb_mode_switch_serial.py
    python3 tools/usb_mode_switch_serial.py --mode 4 --reboot
"""

from __future__ import annotations

import argparse
import glob
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required.  pip install pyserial")


VID = 0x1A86
MODE_NAMES = {
    1: "HID + CDC ACM",
    2: "HID + CDC ACM + UAC (default)",
    3: "HID + CDC ACM Bridge",
    4: "HID + CDC ECM Bridge",
}
STATUS_LABELS = {0: "OK", 1: "ERR_PARAM", 2: "ERR_FRAME"}


def find_serial_port() -> str | None:
    """Find the Openterface CDC ACM serial port."""
    # macOS patterns
    for pattern in ["/dev/cu.usbmodem*", "/dev/cu.wch*", "/dev/cu.Ch340*"]:
        for port in sorted(glob.glob(pattern)):
            return port
    # Windows COM ports (enumerate via pyserial)
    try:
        import serial.tools.list_ports
        for p in serial.tools.list_ports.comports():
            if p.vid == VID:
                return p.device
    except ImportError:
        pass
    return None


def _checksum(data: bytes) -> int:
    return sum(data) & 0xFF


def _ch9329_frame(subcmd: int, payload: bytes = b"") -> bytes:
    """Build CH9329 frame: 57 AB addr(00) cmd(30) len subcmd [payload...] checksum."""
    frame = bytes([0x57, 0xAB, 0x00, 0x30, 1 + len(payload), subcmd])
    frame += payload
    frame += bytes([_checksum(frame)])
    return frame


def _decode_response(raw: bytes) -> bytes | None:
    """Find CH9329 response header and extract payload."""
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


def main():
    parser = argparse.ArgumentParser(description="Switch Openterface USB work mode via serial.")
    parser.add_argument("--mode", type=int, choices=[1, 2, 3, 4], help="Set USB work mode")
    parser.add_argument("--flags", type=lambda v: int(v, 0), default=0, help="Flags (default 0)")
    parser.add_argument("--clear", action="store_true", help="Clear stored mode")
    parser.add_argument("--reboot", action="store_true", help="Reboot after write")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default 115200)")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    port = find_serial_port()
    if port is None:
        sys.exit("No Openterface CDC ACM serial port found. Make sure the device is connected.")

    print(f"Using serial port: {port}")

    ser = serial.Serial(port, baudrate=args.baud, timeout=0.5)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    time.sleep(0.1)

    def send_and_read(subcmd: int, payload: bytes = b"", retries: int = 5) -> bytes | None:
        frame = _ch9329_frame(subcmd, payload)
        if args.verbose:
            print(f"  TX: {frame.hex(' ')}")
        ser.reset_input_buffer()
        ser.write(frame)
        ser.flush()

        for _ in range(retries):
            time.sleep(0.3)
            n = ser.in_waiting
            if n > 0:
                raw = ser.read(n)
                if args.verbose:
                    print(f"  RX: {raw.hex(' ')}")
                return _decode_response(raw)
        return None

    print("\nCurrent status:")
    resp = send_and_read(0x00)
    if resp is None:
        print("  No response received.")
        print("  The kernel CDC ACM driver may not forward CH9329 responses.")
        print("  Try the libusb method instead:")
        print(f"    python3 tools/usb_mode_switch_mac.py")
        print(f"    sudo python3 tools/usb_mode_switch_mac.py  (if connected to host-facing USB)")
        ser.close()
        return 1
    _print_status(resp)

    if args.mode is not None:
        print(f"\nWriting mode {args.mode} ({MODE_NAMES.get(args.mode, '?')})...")
        payload = bytes([args.mode & 0xFF, args.flags & 0xFF, (args.flags >> 8) & 0xFF])
        resp = send_and_read(0x01, payload)
        if resp is not None:
            _print_status(resp)
            if resp[0] != 0:
                print(f"  Write error: {STATUS_LABELS.get(resp[0], 'UNKNOWN')}")
                ser.close()
                return 1
        if args.reboot:
            send_and_read(0x03)
            print("  Reboot requested.")

    elif args.clear:
        print("\nClearing stored mode...")
        resp = send_and_read(0x02)
        if resp is not None:
            _print_status(resp)

    ser.close()
    return 0


if __name__ == "__main__":
    main()
