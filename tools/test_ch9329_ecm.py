#!/usr/bin/env python3
"""Send CH9329 protocol frames through CDC-ECM to the Openterface device.

The firmware at ecm_bridge.c intercepts host-to-device Ethernet frames that
are IPv4/TCP and contain valid CH9329 frames, routing them into the CH9329
command parser.  This script builds the full Ethernet / IPv4 / TCP wrapper
around CH9329 frames so the firmware recognises them as control frames.

Two transport modes are supported:

1. ``--usb-direct`` — uses pyusb/libusb to write raw Ethernet frames to the
   CDC-ECM bulk OUT endpoint.  Works on stock Windows (install libusb via
   Zadig on the CDC-ECM interface first).

2. ``--iface <name>`` — sends raw Ethernet frames through a bound CDC-ECM
   network interface.  Requires Linux/macOS or an ECM-capable Windows driver.

Examples::

    # USB work mode operations (convenience sub-commands)
    python tools/test_ch9329_ecm.py --usb-direct --read
    python tools/test_ch9329_ecm.py --usb-direct --write-mode 3
    python tools/test_ch9329_ecm.py --usb-direct --write-mode 3 --reboot
    python tools/test_ch9329_ecm.py --usb-direct --clear

    # Arbitrary CH9329 commands
    python tools/test_ch9329_ecm.py --usb-direct --cmd 0x01       # get-info
    python tools/test_ch9329_ecm.py --usb-direct --cmd 0x02 --payload 000004000000000000  # key 'A'
    python tools/test_ch9329_ecm.py --usb-direct --raw-frame 57AB00010001  # raw CH9329

    # Via a CDC-ECM NIC (Linux / ECM-capable driver)
    python tools/test_ch9329_ecm.py --iface usb0 --read
"""

import argparse
import socket
import struct
import sys
import time


# ---------------------------------------------------------------------------
# CH9329 constants
# ---------------------------------------------------------------------------
CH9329_HEAD1 = 0x57
CH9329_HEAD2 = 0xAB
CH9329_CMD_GET_INFO = 0x01
CH9329_CMD_KB_GENERAL = 0x02
CH9329_CMD_KB_MEDIA = 0x03
CH9329_CMD_MS_ABS = 0x04
CH9329_CMD_MS_REL = 0x05
CH9329_CMD_GET_PARA_CFG = 0x08
CH9329_CMD_USB_WORK_MODE = 0x30

# ---------------------------------------------------------------------------
# USB VID/PID
# ---------------------------------------------------------------------------
ECM_VID = 0x1A86
ECM_PID = 0xFE0C  # host-side CDC-ECM/ACM interface

# ---------------------------------------------------------------------------
# Convenience: known CH9329 keyboard scan codes (USB HID usage codes)
# ---------------------------------------------------------------------------
_KEY_MAP = {
    "a": 0x04, "b": 0x05, "c": 0x06, "d": 0x07, "e": 0x08,
    "f": 0x09, "g": 0x0A, "h": 0x0B, "i": 0x0C, "j": 0x0D,
    "k": 0x0E, "l": 0x0F, "m": 0x10, "n": 0x11, "o": 0x12,
    "p": 0x13, "q": 0x14, "r": 0x15, "s": 0x16, "t": 0x17,
    "u": 0x18, "v": 0x19, "w": 0x1A, "x": 0x1B, "y": 0x1C,
    "z": 0x1D, "1": 0x1E, "2": 0x1F, "3": 0x20, "4": 0x21,
    "5": 0x22, "6": 0x23, "7": 0x24, "8": 0x25, "9": 0x26,
    "0": 0x27, "enter": 0x28, "esc": 0x29, "backspace": 0x2A,
    "tab": 0x2B, "space": 0x2C, "minus": 0x2D, "equal": 0x27,
    "left": 0x50, "right": 0x4F, "up": 0x52, "down": 0x51,
}

MODIFIER_LEFT_CTRL = 0x01
MODIFIER_LEFT_SHIFT = 0x02
MODIFIER_LEFT_ALT = 0x04
MODIFIER_LEFT_GUI = 0x08


# ---------------------------------------------------------------------------
# Frame builders
# ---------------------------------------------------------------------------

def _checksum(data: bytes) -> int:
    """8-bit additive checksum."""
    return sum(data) & 0xFF


def build_ch9329_frame(addr: int, cmd: int, payload: bytes = b"") -> bytes:
    """Build a CH9329 frame: 57 AB addr cmd len payload... checksum."""
    frame = bytes([CH9329_HEAD1, CH9329_HEAD2, addr & 0xFF, cmd & 0xFF, len(payload)])
    frame += payload
    frame += bytes([_checksum(frame)])
    return frame


def build_ch9329_keyboard(modifier: int = 0, keys: list[int] | None = None) -> bytes:
    """Build a CH9329 keyboard report (cmd 0x02).

    *modifier* — bitmask of left-ctrl/shift/alt/gui.
    *keys* — up to 6 HID usage codes (0 = no key).
    """
    if keys is None:
        keys = []
    payload = bytes([modifier, 0])  # modifier + reserved
    payload += bytes(keys[:6])
    payload += bytes(max(0, 6 - len(keys[:6])))  # pad key slots to 6
    payload += bytes(2)  # trailing reserved padding (CH9329 expects 10-byte payload)
    return build_ch9329_frame(0x00, CH9329_CMD_KB_GENERAL, payload)


def build_ch9329_media(modifier: int = 0) -> bytes:
    """Build a CH9329 media-key report (cmd 0x03)."""
    return build_ch9329_frame(0x00, CH9329_CMD_KB_MEDIA, bytes([modifier, 0]))


def build_ch9329_abs_mouse(buttons: int = 0, x: int = 0, y: int = 0) -> bytes:
    """Build a CH9329 absolute mouse report (cmd 0x04)."""
    payload = struct.pack("<BHH", buttons & 0x07, x & 0xFFFF, y & 0xFFFF)
    return build_ch9329_frame(0x00, CH9329_CMD_MS_ABS, payload)


def build_ch9329_rel_mouse(buttons: int = 0, dx: int = 0, dy: int = 0, wheel: int = 0) -> bytes:
    """Build a CH9329 relative mouse report (cmd 0x05)."""
    payload = struct.pack(
        "<BbbB",
        buttons & 0x07,
        dx & 0xFF if dx >= 0 else -((-dx) & 0xFF),
        dy & 0xFF if dy >= 0 else -((-dy) & 0xFF),
        wheel & 0xFF,
    )
    return build_ch9329_frame(0x00, CH9329_CMD_MS_REL, payload)


def build_eth_ipv4_tcp_frame(
    ch9329_payload: bytes,
    dst_mac: bytes = bytes([0x02, 0x00, 0x00, 0x00, 0x00, 0x01]),
    src_mac: bytes = bytes([0x02, 0x00, 0x00, 0x00, 0x00, 0x02]),
    src_ip: str = "192.168.77.2",
    dst_ip: str = "192.168.77.1",
    src_port: int = 50000,
    dst_port: int = 80,
) -> bytes:
    """Wrap CH9329 payload in Ethernet / IPv4 / TCP.

    The firmware only checks that the frame is Ethernet->IPv4->TCP and that
    the TCP payload consists entirely of valid CH9329 frames.  IP and TCP
    checksums, sequence numbers, etc. do not need to be meaningful.
    """
    # --- Ethernet header (14 bytes) ---
    eth = dst_mac + src_mac + struct.pack("!H", 0x0800)

    # --- IPv4 header (20 bytes, no options) ---
    total_ip_len = 20 + 20 + len(ch9329_payload)
    ip_header = struct.pack(
        "!BBHHHBBH4s4s",
        0x45, 0x00, total_ip_len, 0x0001, 0x4000,
        64, 6, 0,
        socket.inet_aton(src_ip),
        socket.inet_aton(dst_ip),
    )
    ip_chk = ip_checksum(ip_header[:10] + b"\x00\x00" + ip_header[12:])
    ip_header = ip_header[:10] + struct.pack("!H", ip_chk) + ip_header[12:]

    # --- TCP header (20 bytes, no options) ---
    tcp_header = struct.pack(
        "!HHIIBBHHH",
        src_port, dst_port,
        0x00000001, 0x00000000,
        0x50, 0x02, 65535, 0, 0,
    )

    return eth + ip_header + tcp_header + ch9329_payload


def ip_checksum(data: bytes) -> int:
    """16-bit one's complement sum for IP header checksum."""
    if len(data) % 2:
        data += b"\x00"
    s = 0
    for i in range(0, len(data), 2):
        w = (data[i] << 8) + data[i + 1]
        s += w
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


# ---------------------------------------------------------------------------
# Response decoders
# ---------------------------------------------------------------------------

def _decode_ecm_response(data: bytes, verbose: bool) -> dict | None:
    """Strip Ethernet/IPv4/TCP headers and decode the CH9329 response."""
    if len(data) >= 14:
        eth_type = struct.unpack("!H", data[12:14])[0]
        if eth_type == 0x0800:
            data = data[14:]

    if len(data) >= 20 and (data[0] >> 4) == 4:
        ip_header_len = (data[0] & 0x0F) * 4
        if data[9] == 6:
            data = data[ip_header_len:]

    if len(data) >= 20:
        tcp_data_offset = (data[12] >> 4) * 4
        data = data[tcp_data_offset:]

    if len(data) < 6 or data[0] != CH9329_HEAD1 or data[1] != CH9329_HEAD2:
        if verbose:
            print(f"Response does not look like CH9329: {data.hex(' ')}")
        return None

    payload_len = data[4]
    total_len = 6 + payload_len
    if len(data) < total_len:
        if verbose:
            print(f"Truncated CH9329 frame: expected {total_len}, got {len(data)}")
        return None

    return {
        "addr": data[2],
        "cmd": data[3] & 0x3F,
        "cmd_raw": data[3],
        "payload": data[5:5 + payload_len],
    }


def _print_response(resp: dict):
    """Pretty-print a decoded CH9329 response."""
    cmd = resp["cmd"]
    payload = resp["payload"]
    print(f"  CH9329 response: cmd=0x{cmd:02X} (raw=0x{resp['cmd_raw']:02X}), addr={resp['addr']}")

    if cmd == CH9329_CMD_USB_WORK_MODE and len(payload) >= 12:
        print(f"    last_status:   {payload[0]}")
        print(f"    compiled_mode: {payload[1]}")
        print(f"    active_mode:   {payload[2]}")
        print(f"    stored_mode:   {payload[3]}")
        print(f"    config_status: {payload[4]}")
        flags = payload[5] | (payload[6] << 8)
        print(f"    flags:         {flags}")
        cap = payload[7] | (payload[8] << 8) | (payload[9] << 16) | (payload[10] << 24)
        print(f"    capability:    0x{cap:08X}")
    elif cmd == CH9329_CMD_GET_INFO and len(payload) >= 8:
        print(f"    product_version: {payload[0]}")
        print(f"    connected:       {payload[1]}")
        print(f"    led_status:      0x{payload[2]:02X}")
    elif cmd == CH9329_CMD_GET_PARA_CFG:
        print(f"    payload ({len(payload)} bytes): {payload.hex(' ')}")
    else:
        print(f"    payload ({len(payload)} bytes): {payload.hex(' ')}")


# ---------------------------------------------------------------------------
# Transport: USB direct (pyusb / libusb)
# ---------------------------------------------------------------------------

def _send_ecm_frames_usb(
    frames: list[bytes],
    *,
    vid: int,
    pid: int,
    timeout_ms: int,
    verbose: bool,
    expect_response: bool = True,
) -> list[dict]:
    """Send raw Ethernet frames via pyusb CDC-ECM bulk endpoints."""
    try:
        import usb.core
        import usb.util
    except ImportError:
        sys.exit("pyusb is required for --usb-direct.  pip install pyusb")

    dev = usb.core.find(idVendor=vid, idProduct=pid)
    if dev is None:
        sys.exit(
            f"No CDC-ECM device found (VID=0x{vid:04X}, PID=0x{pid:04X}). "
            "Make sure the device is in Mode 4 (ECM) and a libusb driver is attached."
        )

    data_if = None
    ep_out = None
    ep_in = None
    for cfg in dev:
        for intf in cfg:
            if intf.bInterfaceClass == 10:  # CDC Data
                for ep in intf:
                    if ep.bEndpointAddress & 0x80:
                        ep_in = ep
                    else:
                        ep_out = ep
                data_if = intf
                break
        if data_if is not None:
            break

    if data_if is None or ep_out is None:
        sys.exit("Could not find CDC-ECM bulk OUT endpoint.")

    if dev.is_kernel_driver_active(data_if.bInterfaceNumber):
        dev.detach_kernel_driver(data_if.bInterfaceNumber)

    usb.util.claim_interface(dev, data_if.bInterfaceNumber)
    responses = []

    try:
        for frame in frames:
            if verbose:
                print(f"TX {len(frame)} bytes: {frame.hex(' ')}")
            ep_out.write(frame, timeout=timeout_ms)
            time.sleep(0.05)

        if expect_response and ep_in is not None:
            try:
                raw = ep_in.read(1514, timeout=timeout_ms)
                raw = bytes(raw)
                if verbose:
                    print(f"RX {len(raw)} bytes: {raw.hex(' ')}")
                resp = _decode_ecm_response(raw, verbose)
                if resp is not None:
                    responses.append(resp)
            except usb.core.USBTimeoutError:
                if verbose:
                    print("No response from device (timeout).")
    finally:
        usb.util.release_interface(dev, data_if.bInterfaceNumber)

    return responses


# ---------------------------------------------------------------------------
# Transport: raw socket via network interface
# ---------------------------------------------------------------------------

def _send_ecm_frames_iface(
    frames: list[bytes],
    *,
    iface: str,
    timeout_ms: int,
    verbose: bool,
    expect_response: bool = True,
) -> list[dict]:
    """Send raw Ethernet frames via a bound network interface."""
    sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0003))
    sock.bind((iface, 0))
    responses = []

    try:
        for frame in frames:
            if verbose:
                print(f"TX {len(frame)} bytes: {frame.hex(' ')}")
            sock.send(frame)
            time.sleep(0.05)

        if expect_response:
            sock.settimeout(timeout_ms / 1000.0)
            try:
                raw = sock.recv(1514)
                if verbose:
                    print(f"RX {len(raw)} bytes: {raw.hex(' ')}")
                resp = _decode_ecm_response(raw, verbose)
                if resp is not None:
                    responses.append(resp)
            except socket.timeout:
                if verbose:
                    print("No response from device (timeout).")
    finally:
        sock.close()

    return responses


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _build_frames_from_args(args) -> tuple[list[bytes], bool]:
    """Build Ethernet frames from CLI arguments. Returns (frames, expect_response)."""
    dst_mac = bytes([0x02, 0x00, 0x00, 0x00, 0x00, 0x01])
    src_mac = bytes([0x02, 0x00, 0x00, 0x00, 0x00, 0x02])
    frames = []
    expect_response = True

    # --raw-frame: pass through a complete CH9329 frame as hex
    if args.raw_frame:
        raw = bytes.fromhex(args.raw_frame.replace(" ", "").replace(":", ""))
        frames.append(build_eth_ipv4_tcp_frame(raw, dst_mac, src_mac))

    # --cmd: arbitrary CH9329 command
    if args.cmd is not None:
        payload = b""
        if args.payload:
            payload = bytes.fromhex(args.payload.replace(" ", "").replace(":", ""))
        frames.append(build_eth_ipv4_tcp_frame(
            build_ch9329_frame(args.addr, args.cmd, payload), dst_mac, src_mac,
        ))

    # Keyboard shortcut
    if args.keyboard:
        modifier = 0
        if args.ctrl:
            modifier |= MODIFIER_LEFT_CTRL
        if args.shift:
            modifier |= MODIFIER_LEFT_SHIFT
        if args.alt:
            modifier |= MODIFIER_LEFT_ALT
        if args.gui:
            modifier |= MODIFIER_LEFT_GUI
        keys = []
        for k in args.keyboard:
            k_lower = k.lower()
            if k_lower in _KEY_MAP:
                keys.append(_KEY_MAP[k_lower])
            else:
                try:
                    keys.append(int(k, 0))
                except ValueError:
                    sys.exit(f"Unknown key '{k}'. Use a letter, number, or HID usage name.")
        frames.append(build_eth_ipv4_tcp_frame(
            build_ch9329_keyboard(modifier, keys[:6]), dst_mac, src_mac,
        ))

    # Media key
    if args.media_key is not None:
        frames.append(build_eth_ipv4_tcp_frame(
            build_ch9329_media(args.media_key), dst_mac, src_mac,
        ))

    # Absolute mouse
    if args.abs_mouse:
        frames.append(build_eth_ipv4_tcp_frame(
            build_ch9329_abs_mouse(*args.abs_mouse), dst_mac, src_mac,
        ))

    # Relative mouse
    if args.rel_mouse:
        frames.append(build_eth_ipv4_tcp_frame(
            build_ch9329_rel_mouse(*args.rel_mouse), dst_mac, src_mac,
        ))

    # USB work mode convenience commands
    if args.clear:
        frames.append(build_eth_ipv4_tcp_frame(
            build_ch9329_frame(0x00, CH9329_CMD_USB_WORK_MODE, bytes([0x02])),
            dst_mac, src_mac,
        ))
        expect_response = False

    if args.write_mode is not None:
        payload = bytes([0x01, args.write_mode & 0xFF, args.flags & 0xFF, (args.flags >> 8) & 0xFF])
        frames.append(build_eth_ipv4_tcp_frame(
            build_ch9329_frame(0x00, CH9329_CMD_USB_WORK_MODE, payload),
            dst_mac, src_mac,
        ))
        expect_response = False

    if args.reboot:
        frames.append(build_eth_ipv4_tcp_frame(
            build_ch9329_frame(0x00, CH9329_CMD_USB_WORK_MODE, bytes([0x03])),
            dst_mac, src_mac,
        ))
        expect_response = False

    # Default: read USB work mode status
    if not frames:
        frames.append(build_eth_ipv4_tcp_frame(
            build_ch9329_frame(0x00, CH9329_CMD_USB_WORK_MODE, bytes([0x00])),
            dst_mac, src_mac,
        ))

    return frames, expect_response


def main():
    parser = argparse.ArgumentParser(
        description="Send CH9329 protocol frames through CDC-ECM to the Openterface device.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )

    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument(
        "--usb-direct", action="store_true",
        help="Use pyusb to write to CDC-ECM bulk endpoints directly.",
    )
    transport.add_argument(
        "--iface", type=str,
        help="CDC-ECM network interface name (e.g., usb0).",
    )

    # USB work mode convenience commands
    mode_grp = parser.add_argument_group("USB work mode (cmd 0x30)")
    mode_grp.add_argument("--read", action="store_true", help="Read USB mode status (default if no op given).")
    mode_grp.add_argument("--write-mode", type=int, choices=[1, 2, 3, 4], help="Set persistent USB mode.")
    mode_grp.add_argument("--flags", type=lambda v: int(v, 0), default=0, help="Flags for write.")
    mode_grp.add_argument("--clear", action="store_true", help="Clear persistent mode record.")
    mode_grp.add_argument("--reboot", action="store_true", help="Request device reboot.")

    # Arbitrary CH9329 commands
    ch9329_grp = parser.add_argument_group("Arbitrary CH9329 commands")
    ch9329_grp.add_argument("--cmd", type=lambda v: int(v, 0), help="CH9329 command byte (hex).")
    ch9329_grp.add_argument("--addr", type=lambda v: int(v, 0), default=0, help="CH9329 address (default 0).")
    ch9329_grp.add_argument("--payload", type=str, help="CH9329 payload as hex string.")
    ch9329_grp.add_argument("--raw-frame", type=str, help="Complete CH9329 frame as hex (auto-checksum not added).")

    # Keyboard shortcut
    kb_grp = parser.add_argument_group("Keyboard shortcut (cmd 0x02)")
    kb_grp.add_argument("--keyboard", nargs="+", help="Keys to press (letters, numbers, or names like 'enter', 'esc').")
    kb_grp.add_argument("--ctrl", action="store_true")
    kb_grp.add_argument("--shift", action="store_true")
    kb_grp.add_argument("--alt", action="store_true")
    kb_grp.add_argument("--gui", action="store_true")

    # Other shortcuts
    ch9329_grp.add_argument("--media-key", type=int, help="Send media key report (cmd 0x03).")
    ch9329_grp.add_argument("--abs-mouse", type=int, nargs=3, metavar=("BTN", "X", "Y"), help="Absolute mouse (cmd 0x04).")
    ch9329_grp.add_argument("--rel-mouse", type=int, nargs=4, metavar=("BTN", "DX", "DY", "WHEEL"), help="Relative mouse (cmd 0x05).")

    parser.add_argument("--vid", type=lambda v: int(v, 0), default=ECM_VID)
    parser.add_argument("--pid", type=lambda v: int(v, 0), default=ECM_PID)
    parser.add_argument("--timeout", type=int, default=3000, help="Timeout in ms (default 3000).")
    parser.add_argument("-v", "--verbose", action="store_true")

    args = parser.parse_args()

    frames, expect_response = _build_frames_from_args(args)
    if not frames:
        parser.error("No operation specified. Use --read, --write-mode, --cmd, --keyboard, etc.")

    if args.usb_direct:
        responses = _send_ecm_frames_usb(
            frames, vid=args.vid, pid=args.pid,
            timeout_ms=args.timeout, verbose=args.verbose,
            expect_response=expect_response,
        )
    else:
        responses = _send_ecm_frames_iface(
            frames, iface=args.iface,
            timeout_ms=args.timeout, verbose=args.verbose,
            expect_response=expect_response,
        )

    for resp in responses:
        _print_response(resp)

    if not responses and expect_response:
        print("No response received.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
