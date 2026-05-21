#!/usr/bin/env python3
"""Auto-detect CDC-ACM serial port and send CH9329 0x30 mode commands.

Usage examples:
  python tools/cdc_acm_switch.py --mode 3 --reboot
  python tools/cdc_acm_switch.py --list

Behavior:
- Scans available serial ports for devices with VID_1A86 (0x1A86) or other
  Openterface-related identifiers and selects the host CDC ACM port if found.
- Builds CH9329 0x30 framed requests and sends them, optionally waiting for
  a response.
- Supports read (no payload), write (01 mode [flags_lo flags_hi]), clear, reboot.

This tool is cross-platform and uses pyserial for serial I/O.
"""

from __future__ import annotations

import argparse
import logging
import sys
import struct
import time
from typing import Optional, Tuple

import serial
import serial.tools.list_ports

# CH9329 frame constants
CH_HEAD1 = 0x57
CH_HEAD2 = 0xAB
CMD_USB_WORK_MODE = 0x30

DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT = 2.0  # seconds

logger = logging.getLogger("cdc_acm_switch")


def calc_checksum(data: bytes) -> int:
    return sum(data) & 0xFF


def build_frame(addr: int, cmd: int, payload: bytes) -> bytes:
    length = len(payload)
    pre = bytes([CH_HEAD1, CH_HEAD2, addr & 0xFF, cmd & 0xFF, length & 0xFF])
    frame = pre + payload
    cs = calc_checksum(frame)
    return frame + bytes([cs])


def parse_frame(data: bytes) -> Optional[Tuple[int, int, bytes]]:
    # minimal parser: find header, validate length and checksum
    if len(data) < 6:
        return None
    for i in range(len(data) - 1):
        if data[i] == CH_HEAD1 and data[i + 1] == CH_HEAD2:
            if i + 5 >= len(data):
                return None
            addr = data[i + 2]
            cmd = data[i + 3]
            length = data[i + 4]
            end = i + 5 + length
            if end >= len(data):
                return None
            payload = data[i + 5 : i + 5 + length]
            cs = data[i + 5 + length]
            if calc_checksum(data[i : i + 5 + length]) & 0xFF != cs:
                return None
            return addr, cmd, payload
    return None


def find_candidate_ports(vids=(0x1A86,)) -> list[str]:
    ports = []
    for p in serial.tools.list_ports.comports():
        try:
            vid = p.vid
            pid = p.pid
        except Exception:
            vid = None
            pid = None
        desc = p.description or ""
        hwid = p.hwid or ""
        if vid in vids or (hwid and any(f"VID:{v:04X}" in hwid for v in vids)):
            ports.append(p.device)
        else:
            # heuristics: match 'CH9329' or known Openterface product strings
            if "CH9329" in desc.upper() or "OPENTERFACE" in desc.upper() or "VID_1A86" in hwid.upper():
                ports.append(p.device)
    return ports


def auto_detect_port() -> Optional[str]:
    # look for common VID 0x1A86 devices first, fall back to any serial port
    candidates = find_candidate_ports((0x1A86,))
    if candidates:
        return candidates[0]
    # fallback: return first tty/COM port that is not bluetooth, loopback
    for p in serial.tools.list_ports.comports():
        name = p.device
        if "Bluetooth" in (p.description or ""):
            continue
        if name:
            return name
    return None


def send_and_receive(port: str, baud: int, frame: bytes, timeout: float) -> Tuple[bool, bytes]:
    with serial.Serial(port, baud, timeout=timeout) as ser:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        ser.write(frame)
        ser.flush()
        # wait briefly for response
        deadline = time.time() + timeout
        buf = bytearray()
        while time.time() < deadline:
            data = ser.read(256)
            if data:
                buf.extend(data)
                parsed = parse_frame(bytes(buf))
                if parsed is not None:
                    return True, bytes(buf)
            else:
                time.sleep(0.05)
        return False, bytes(buf)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Send CH9329 0x30 USB work mode commands over CDC-ACM")
    parser.add_argument("--port", help="Serial port to use (auto-detected if omitted)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Baud rate for CDC-ACM (default 115200)")
    parser.add_argument("--addr", type=int, default=0x00, help="CH9329 address (default 0x00)")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT, help="Response timeout seconds")
    group = parser.add_mutually_exclusive_group(required=False)
    group.add_argument("--read", action="store_true", help="Read current USB mode status")
    group.add_argument("--write", type=int, metavar="MODE", help="Write stored mode (1-4)")
    group.add_argument("--clear", action="store_true", help="Clear stored mode record")
    group.add_argument("--reboot", action="store_true", help="Request reboot (sends reboot command)")
    parser.add_argument("--no-response", action="store_true", help="Do not wait for framed response (fire-and-forget)")
    parser.add_argument("--list", action="store_true", help="List candidate serial ports and exit")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO)

    if args.list:
        ports = [p.device for p in serial.tools.list_ports.comports()]
        print("Available ports:")
        for p in ports:
            print(" -", p)
        print("Candidate CDC-ACM ports:")
        for c in find_candidate_ports((0x1A86,)):
            print(" -", c)
        return 0

    port = args.port or auto_detect_port()
    if not port:
        print("No serial port found; use --port to specify a device.")
        return 2

    if args.read:
        payload = b""
    elif args.clear:
        payload = bytes([0x02])
    elif args.reboot:
        payload = bytes([0x03])
    elif args.write is not None:
        mode = int(args.write)
        if not (1 <= mode <= 4):
            print("Mode must be 1..4")
            return 3
        # write payload: 01 <mode> (no flags)
        payload = bytes([0x01, mode])
    else:
        print("No operation selected")
        return 4

    frame = build_frame(args.addr & 0xFF, CMD_USB_WORK_MODE, payload)

    if args.no_response:
        try:
            with serial.Serial(port, args.baud, timeout=0.1) as s:
                s.write(frame)
                s.flush()
            print("Frame written (no response requested)")
            return 0
        except Exception as exc:
            print("Failed to write:", exc)
            return 5

    ok, raw = send_and_receive(port, args.baud, frame, args.timeout)
    if not ok:
        print("No framed response received. Raw read:")
        if raw:
            print(raw.hex())
        return 6

    parsed = parse_frame(raw)
    if not parsed:
        print("Failed to parse response")
        print(raw.hex())
        return 7

    addr, cmd, payload = parsed
    print(f"Response addr=0x{addr:02X} cmd=0x{cmd:02X} payload={payload.hex()}")
    if cmd & 0x80 == (CMD_USB_WORK_MODE | 0x80) or cmd == (CMD_USB_WORK_MODE | 0x80):
        # good
        if len(payload) >= 12:
            status = payload[0]
            compiled = payload[1]
            active = payload[2]
            stored = payload[3]
            flags = payload[5] | (payload[6] << 8)
            capmask = int.from_bytes(payload[7:11], "little")
            print(f"status={status:02X} compiled={compiled} active={active} stored={stored} flags=0x{flags:04X} capmask=0x{capmask:08X}")
        else:
            print("Short payload:", payload.hex())
        return 0
    else:
        print(f"Unexpected response cmd=0x{cmd:02X}")
        return 8


if __name__ == "__main__":
    raise SystemExit(main())
