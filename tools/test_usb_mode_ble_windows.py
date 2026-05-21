#!/usr/bin/env python3
"""Switch Openterface USB runtime mode through the BLE rawpass service.

The firmware accepts the same CH9329 USB work-mode command 0x30 frames over
BLE rawpass RX as it does over CDC ACM. This helper writes those frames and can
verify the result with the HID Feature report after reboot.
"""

import argparse
import asyncio
import sys
import time
from pathlib import Path

try:
    from bleak import BleakClient, BleakScanner
except ImportError as exc:
    raise SystemExit("Missing dependency: install bleak in the active Python environment.") from exc


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import test_usb_mode_hid_windows as mode_hid  # noqa: E402


SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb"
RX_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"
DEFAULT_NAME_MATCHES = ["kvm-go", "keymod"]


def ch9329_frame(payload: bytes, addr: int = 0) -> bytes:
    frame = bytearray([0x57, 0xAB, addr & 0xFF, 0x30, len(payload) & 0xFF])
    frame.extend(payload)
    frame.append(sum(frame) & 0xFF)
    return bytes(frame)


def mode_frame(mode: int, flags: int) -> bytes:
    return ch9329_frame(bytes([0x01, mode & 0xFF, flags & 0xFF, (flags >> 8) & 0xFF]))


def reboot_frame() -> bytes:
    return ch9329_frame(bytes([0x03]))


def close_handle(handle):
    if handle != mode_hid.INVALID_HANDLE_VALUE:
        mode_hid.kernel32.CloseHandle(handle)


def find_mode_hid(vid: int, pid: int):
    for path in mode_hid.iter_hid_paths():
        handle = mode_hid.open_hid(path)
        if handle == mode_hid.INVALID_HANDLE_VALUE:
            continue
        try:
            attrs = mode_hid.get_attrs(handle)
            if attrs.VendorID != vid or attrs.ProductID != pid:
                continue
            report = mode_hid.get_feature(handle)
            if report is None or report[0] != mode_hid.REPORT_ID:
                continue
            return path, handle, mode_hid.decode_report(report)
        except Exception:
            close_handle(handle)
            raise
        close_handle(handle)
    raise RuntimeError("No Openterface USB mode HID Feature report found.")


def read_status(vid: int, pid: int) -> dict:
    path, handle, status = find_mode_hid(vid, pid)
    close_handle(handle)
    return status


def wait_for_mode(vid: int, pid: int, expected_mode: int, timeout_s: float, poll_s: float) -> dict:
    deadline = time.monotonic() + timeout_s
    last_error = "device not checked yet"
    while time.monotonic() < deadline:
        try:
            status = read_status(vid, pid)
            if status["active_mode"] == expected_mode and status["stored_mode"] == expected_mode:
                return status
            last_error = f"active={status['active_mode']} stored={status['stored_mode']}"
        except Exception as exc:
            last_error = str(exc)
        time.sleep(poll_s)
    raise TimeoutError(f"Timed out waiting for Mode {expected_mode}; last status: {last_error}")


def format_status(status: dict) -> str:
    return (
        f"compiled={status['compiled_mode']} active={status['active_mode']} "
        f"stored={status['stored_mode']} flags={status['flags']} "
        f"capability=0x{status['capability_mask']:08X} last_status={status['last_status']}"
    )


def matches_device(device, advertisement, name_matches: list[str], service_uuid: str) -> bool:
    name = (device.name or advertisement.local_name or "").lower()
    services = [item.lower() for item in (advertisement.service_uuids or [])]
    return any(match in name for match in name_matches) or service_uuid.lower() in services


async def find_ble_device(args):
    if args.address:
        return await BleakScanner.find_device_by_address(args.address, timeout=args.scan_timeout)

    name_matches = [item.lower() for item in args.name_match]

    def filter_device(device, advertisement):
        return matches_device(device, advertisement, name_matches, args.service_uuid)

    return await BleakScanner.find_device_by_filter(filter_device, timeout=args.scan_timeout)


async def send_ble_mode(args, mode: int) -> None:
    device = await find_ble_device(args)
    if device is None:
        names = ", ".join(args.name_match)
        raise RuntimeError(f"No BLE device found by address, service {args.service_uuid}, or names: {names}")

    print(f"BLE device: {device.address} {device.name or '<unnamed>'}")
    async with BleakClient(device, timeout=args.connect_timeout) as client:
        print(f"BLE connected: {client.is_connected}")
        await client.write_gatt_char(args.rx_uuid, mode_frame(mode, args.flags), response=True)
        print(f"BLE wrote Mode {mode} command")
        if args.reboot:
            await asyncio.sleep(args.reboot_delay)
            try:
                await client.write_gatt_char(args.rx_uuid, reboot_frame(), response=False)
                print("BLE wrote reboot command")
            except Exception as exc:
                print(f"BLE reboot write ended with disconnect/error: {exc}")


async def run(args) -> int:
    initial = read_status(args.vid, args.pid)
    print("Initial HID:", format_status(initial))

    await send_ble_mode(args, args.write_mode)

    if args.verify:
        status = wait_for_mode(args.vid, args.pid, args.write_mode, args.verify_timeout, args.poll_interval)
        print("Verified HID:", format_status(status))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Switch Openterface USB mode over BLE rawpass.")
    parser.add_argument("--write-mode", type=int, choices=[1, 2, 3, 4], required=True)
    parser.add_argument("--flags", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--reboot", action="store_true", help="Send reboot command after writing the stored mode.")
    parser.add_argument("--verify", action="store_true", help="Verify active/stored mode through HID after reboot.")
    parser.add_argument("--vid", type=lambda value: int(value, 0), default=0x1A86)
    parser.add_argument("--pid", type=lambda value: int(value, 0), default=0xFE00)
    parser.add_argument("--address", help="BLE address to connect to instead of scanning by name/service.")
    parser.add_argument("--name-match", action="append", default=DEFAULT_NAME_MATCHES.copy(), help="Case-insensitive BLE name substring to accept. Defaults include KVM-GO and KeyMod.")
    parser.add_argument("--service-uuid", default=SERVICE_UUID)
    parser.add_argument("--rx-uuid", default=RX_UUID)
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--connect-timeout", type=float, default=20.0)
    parser.add_argument("--verify-timeout", type=float, default=25.0)
    parser.add_argument("--poll-interval", type=float, default=1.0)
    parser.add_argument("--reboot-delay", type=float, default=0.4)
    args = parser.parse_args()

    return asyncio.run(run(args))


if __name__ == "__main__":
    raise SystemExit(main())