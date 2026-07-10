#!/usr/bin/env python3
"""Firmware update tool for Openterface KM bootloader.

Usage:
  # Enter bootloader mode and flash firmware
  python3 tools/flash_fw.py --app <firmware.bin> --cdc <cdc_port>

  # Flash to bootloader directly (already in bootloader mode)
  python3 tools/flash_fw.py --app <firmware.bin>

  # Just enter bootloader mode (no flash)
  python3 tools/flash_fw.py --enter-bootloader --cdc <cdc_port>

Protocol (USB bulk EP2):
  Request:  [0xAA][0x55][CMD][LEN][DATA...][CS][CS][0x55][0xAA]
  Response: [0xAA][0x55][0x00][STATUS][0x55][0xAA]
  CS = 8-bit checksum (sum of CMD+LEN+DATA mod 256)
"""

import argparse
import struct
import sys
import time
from typing import Optional

try:
    import usb.core
    import usb.util
except ImportError:
    print("Error: pyusb not installed. Run: pip3 install pyusb")
    sys.exit(1)

# Bootloader USB identifiers
BL_VID = 0x1A86
BL_PID = 0xFE10
BL_EP_OUT = 0x02  # EP2 OUT
BL_EP_IN = 0x82   # EP2 IN
BL_TIMEOUT = 1000  # ms

# Bootloader protocol commands
CMD_HANDSHAKE = 0x01
CMD_ERASE = 0x02
CMD_WRITE = 0x03
CMD_VERIFY = 0x04
CMD_JUMP = 0x05

# Frame markers
FRAME_HEAD1 = 0xAA
FRAME_HEAD2 = 0x55
FRAME_END1 = 0x55
FRAME_END2 = 0xAA

# App command protocol constants (CH9329 frame for entering bootloader)
CH9329_HEAD1 = 0x57
CH9329_HEAD2 = 0xAB
CMD_BOOTLOADER_JUMP = 0x40

# Flash constants
FLASH_PAGE_SIZE = 4096
FLASH_APP_START = 0x00004000
FLASH_APP_END = 0x0006E000
WRITE_CHUNK_SIZE = 56  # Max payload per USB packet: 64 - 8 (frame overhead)


def checksum8(data: bytes) -> int:
    """8-bit checksum: sum of all bytes mod 256."""
    return sum(data) & 0xFF


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    """Build a bootloader protocol frame.
    Format: [AA][55][CMD][LEN][DATA...][CS][CS][55][AA]
    """
    frame = bytes([FRAME_HEAD1, FRAME_HEAD2, cmd, len(payload)])
    frame += payload
    cs = checksum8(frame[2:])  # checksum over CMD+LEN+DATA
    frame += bytes([cs, cs])
    frame += bytes([FRAME_END1, FRAME_END2])
    return frame


def find_bootloader() -> usb.core.Device:
    """Find the bootloader USB device by VID:PID."""
    dev = usb.core.find(idVendor=BL_VID, idProduct=BL_PID)
    if dev is None:
        raise RuntimeError(
            f"Bootloader not found (VID=0x{BL_VID:04X}, PID=0x{BL_PID:04X}). "
            "Is the device in bootloader mode?"
        )
    return dev


def send_frame(dev: usb.core.Device, cmd: int, payload: bytes = b"") -> int:
    """Send a frame and return the status (0=OK, 1=ERROR)."""
    frame = build_frame(cmd, payload)

    # Send via EP2 OUT
    try:
        sent = dev.write(BL_EP_OUT, frame, timeout=BL_TIMEOUT)
        if sent != len(frame):
            raise RuntimeError(f"Short write: {sent}/{len(frame)}")
    except usb.core.USBError as e:
        raise RuntimeError(f"USB write error: {e}")

    # Read response via EP2 IN
    try:
        resp = dev.read(BL_EP_IN, 64, timeout=BL_TIMEOUT)
    except usb.core.USBError as e:
        raise RuntimeError(f"USB read error: {e}")

    if len(resp) < 6:
        raise RuntimeError(f"Short response: {len(resp)} bytes")

    # Validate response: [AA][55][00][STATUS][55][AA]
    if resp[0] != FRAME_HEAD1 or resp[1] != FRAME_HEAD2:
        raise RuntimeError(f"Invalid response header: {resp[:2].hex()}")
    if resp[4] != FRAME_END1 or resp[5] != FRAME_END2:
        raise RuntimeError(f"Invalid response footer: {resp[4:6].hex()}")

    status = resp[3]
    return status


def send_frame_with_data(dev: usb.core.Device, cmd: int, payload: bytes = b"") -> tuple:
    """Send a frame and return (status, data_byte). For CMD_VERIFY."""
    frame = build_frame(cmd, payload)

    try:
        sent = dev.write(BL_EP_OUT, frame, timeout=BL_TIMEOUT)
        if sent != len(frame):
            raise RuntimeError(f"Short write: {sent}/{len(frame)}")
    except usb.core.USBError as e:
        raise RuntimeError(f"USB write error: {e}")

    try:
        resp = dev.read(BL_EP_IN, 64, timeout=BL_TIMEOUT)
    except usb.core.USBError as e:
        raise RuntimeError(f"USB read error: {e}")

    if len(resp) < 6:
        raise RuntimeError(f"Short response: {len(resp)} bytes")

    if resp[0] != FRAME_HEAD1 or resp[1] != FRAME_HEAD2:
        raise RuntimeError(f"Invalid response header: {resp[:2].hex()}")
    if resp[4] != FRAME_END1 or resp[5] != FRAME_END2:
        raise RuntimeError(f"Invalid response footer: {resp[4:6].hex()}")

    return resp[3], resp[2]  # (status, data_byte)


def enter_bootloader(cdc_port: str) -> None:
    """Send CMD_BOOTLOADER_JUMP over CDC ACM to enter bootloader mode."""
    try:
        import serial
    except ImportError:
        print("Error: pyserial not installed. Run: pip3 install pyserial")
        sys.exit(1)

    print(f"  Sending bootloader command to {cdc_port}...")
    ser = serial.Serial(cdc_port, 115200, timeout=3)
    time.sleep(0.1)

    frame = bytes([CH9329_HEAD1, CH9329_HEAD2, 0x00, CMD_BOOTLOADER_JUMP, 0x00])
    checksum = sum(frame) & 0xFF
    ser.write(frame + bytes([checksum]))
    ser.flush()

    # Read ACK (device may reset before sending it)
    try:
        resp = ser.read(64)
        if resp:
            print(f"  Response: {resp.hex()}")
    except Exception:
        pass

    ser.close()
    print("  Device should reset into bootloader mode now.")
    print("  Wait ~3 seconds for USB re-enumeration...")


def flash_firmware(firmware_path: str) -> None:
    """Flash firmware to device via USB bootloader."""
    with open(firmware_path, "rb") as f:
        fw = f.read()

    fw_size = len(fw)
    print(f"  Firmware: {firmware_path} ({fw_size} bytes)")

    # Pad to 4-byte alignment
    if fw_size % 4 != 0:
        pad = 4 - (fw_size % 4)
        fw += b"\xFF" * pad
        print(f"  Padded to {len(fw)} bytes (4-byte alignment)")

    # Pad to page boundary
    if len(fw) % FLASH_PAGE_SIZE != 0:
        pad = FLASH_PAGE_SIZE - (len(fw) % FLASH_PAGE_SIZE)
        fw += b"\xFF" * pad
        print(f"  Padded to {len(fw)} bytes (page boundary)")

    if FLASH_APP_START + len(fw) > FLASH_APP_END:
        raise RuntimeError(f"Firmware too large: {len(fw)} bytes, max {FLASH_APP_END - FLASH_APP_START}")

    # Find bootloader
    print("  Finding bootloader...")
    dev = find_bootloader()

    # Handshake
    print("  Handshake...")
    status = send_frame(dev, CMD_HANDSHAKE)
    if status != 0:
        raise RuntimeError(f"Handshake failed: status=0x{status:02X}")
    print("  Handshake OK")

    # Erase
    num_pages = len(fw) // FLASH_PAGE_SIZE
    print(f"  Erasing {num_pages} pages ({len(fw)} bytes)...")
    status = send_frame(dev, CMD_ERASE)
    if status != 0:
        raise RuntimeError(f"Erase failed: status=0x{status:02X}")
    print("  Erase OK")

    # Write chunks
    offset = 0
    chunk_idx = 0
    total_chunks = (len(fw) + WRITE_CHUNK_SIZE - 1) // WRITE_CHUNK_SIZE
    while offset < len(fw):
        chunk = fw[offset:offset + WRITE_CHUNK_SIZE]
        status = send_frame(dev, CMD_WRITE, chunk)
        if status != 0:
            raise RuntimeError(f"Write failed at offset {offset}: status=0x{status:02X}")
        offset += len(chunk)
        chunk_idx += 1
        if chunk_idx % 32 == 0 or chunk_idx == total_chunks:
            print(f"  Written {offset}/{len(fw)} bytes ({100 * offset // len(fw)}%)")

    # Verify
    print("  Verifying...")
    # Send verify command with address and length, get checksum back
    verify_payload = struct.pack("<IH", FLASH_APP_START, len(fw))
    status, flash_cs = send_frame_with_data(dev, CMD_VERIFY, verify_payload)
    if status != 0:
        raise RuntimeError(f"Verify failed: status=0x{status:02X}")
    local_cs = sum(fw) & 0xFF
    print(f"  Flash checksum: 0x{flash_cs:02X}, Local checksum: 0x{local_cs:02X}")
    if flash_cs != local_cs:
        raise RuntimeError(f"Checksum mismatch! Flash=0x{flash_cs:02X} Local=0x{local_cs:02X}")
    print("  Verify OK")

    # Jump to app
    print("  Jumping to application...")
    status = send_frame(dev, CMD_JUMP)
    if status != 0:
        print(f"  Warning: Jump status=0x{status:02X}")
    print("  Done!")


def main():
    parser = argparse.ArgumentParser(description="Openterface KM firmware update tool (USB)")
    parser.add_argument("--app", help="Path to firmware binary (.bin)")
    parser.add_argument("--cdc", help="CDC ACM serial port (for entering bootloader)")
    parser.add_argument("--enter-bootloader", action="store_true", help="Only enter bootloader mode")
    args = parser.parse_args()

    if args.enter_bootloader:
        if not args.cdc:
            print("Error: --cdc required with --enter-bootloader")
            sys.exit(1)
        print("=== Entering bootloader mode ===")
        enter_bootloader(args.cdc)
        return

    if not args.app:
        parser.print_help()
        sys.exit(1)

    # If --cdc is specified, enter bootloader first
    if args.cdc:
        print("=== Entering bootloader mode ===")
        enter_bootloader(args.cdc)
        print("  Waiting 3 seconds for USB re-enumeration...")
        time.sleep(3)

    print("=== Flashing firmware via USB ===")
    flash_firmware(args.app)


if __name__ == "__main__":
    main()
