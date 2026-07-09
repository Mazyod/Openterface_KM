#!/usr/bin/env python3
"""Exercise Openterface runtime USB mode switching on Windows.

This script uses the vendor HID Feature report 0x30 exposed by the target-side
VID_1A86 PID_FE00 HID interface, then verifies the active mode and Windows PnP
enumeration after each reboot.
"""

from __future__ import annotations

import sys
if sys.platform != "win32":
    import pytest
    pytest.skip("Windows-only test", allow_module_level=True)

import argparse
import ctypes
import json
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import test_usb_mode_hid_windows as mode_hid  # noqa: E402


MODE_NAMES = {
    1: "hid_acm",
    2: "hid_uac_acm",
    3: "hid_acm_bridge",
    4: "hid_ecm_bridge",
}

EXPECTED_CAPABILITY_MASK = 0x0000001E


@dataclass
class TransitionResult:
    source: int
    target: int
    passed: bool
    status: dict | None
    notes: list[str]


def mode_label(mode: int) -> str:
    return f"Mode {mode} ({MODE_NAMES.get(mode, 'unknown')})"


def close_handle(handle):
    if handle != mode_hid.INVALID_HANDLE_VALUE:
        mode_hid.kernel32.CloseHandle(handle)


def find_mode_hid(vid: int, pid: int):
    last_error = None
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
        except OSError as exc:
            last_error = exc
            close_handle(handle)
            continue
        except Exception:
            close_handle(handle)
            raise

        close_handle(handle)

    if last_error:
        raise RuntimeError(f"No readable mode HID report found; last HID error: {last_error}")
    raise RuntimeError("No Openterface USB mode HID Feature report found.")


def read_status(vid: int, pid: int) -> tuple[str, dict]:
    path, handle, status = find_mode_hid(vid, pid)
    close_handle(handle)
    return path, status


def send_write_mode(vid: int, pid: int, mode: int, flags: int) -> dict:
    path, handle, _ = find_mode_hid(vid, pid)
    try:
        mode_hid.set_feature(handle, 0x01, mode, flags)
        report = mode_hid.get_feature(handle)
        if report is None:
            raise RuntimeError(f"Write to {mode_label(mode)} succeeded but readback failed on {path}.")
        status = mode_hid.decode_report(report)
        if status["last_status"] != 0:
            raise RuntimeError(f"Write to {mode_label(mode)} returned status {status['last_status']}.")
        if status["stored_mode"] != mode:
            raise RuntimeError(
                f"Write to {mode_label(mode)} did not persist; stored_mode={status['stored_mode']}."
            )
        return status
    finally:
        close_handle(handle)


def mode_has_host_cdc_acm(mode: int) -> bool:
    return mode in (1, 2, 3)


def send_reboot(vid: int, pid: int) -> None:
    path, handle, _ = find_mode_hid(vid, pid)
    try:
        try:
            mode_hid.set_feature(handle, 0x03)
        except OSError as exc:
            if getattr(exc, "winerror", None) != 31:
                raise
            print(f"  reboot command disconnected the device as expected: WinError {exc.winerror}")
    finally:
        close_handle(handle)


def run_ch9329_mode_command(args, operation: str, mode: int | None = None, no_response: bool = False) -> tuple[int, str]:
    command = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", str(SCRIPT_DIR / "test_usb_mode_ch9329_windows.ps1"),
        "-Operation", operation,
        "-BaudRate", str(args.ch9329_baud_rate),
        "-TimeoutMs", str(args.ch9329_timeout_ms),
    ]
    if args.ch9329_port:
        command.extend(["-Port", args.ch9329_port])
    if mode is not None:
        command.extend(["-Mode", str(mode)])
    if args.flags:
        command.extend(["-Flags", str(args.flags)])
    if no_response:
        command.append("-NoResponse")
    return run_command(command, args.ch9329_timeout_ms / 1000.0 + 5.0)


def send_write_mode_ch9329(args, source_mode: int, target_mode: int) -> None:
    if not mode_has_host_cdc_acm(source_mode):
        raise RuntimeError(
            f"CH9329 control requires an active host CDC ACM interface; "
            f"{mode_label(source_mode)} does not expose it."
        )

    write_code, write_output = run_ch9329_mode_command(args, "Write", target_mode, True)
    if write_code != 0:
        raise RuntimeError("CH9329 write failed:\n" + write_output)
    print("  CH9329 write sent:\n" + indent_text(write_output, "    "))


def send_reboot_ch9329(args) -> None:
    reboot_code, reboot_output = run_ch9329_mode_command(args, "Reboot", None, True)
    if reboot_code != 0:
        raise RuntimeError("CH9329 reboot failed:\n" + reboot_output)
    print("  CH9329 reboot sent:\n" + indent_text(reboot_output, "    "))


def indent_text(text: str, prefix: str) -> str:
    if not text:
        return prefix + "<no output>"
    return "\n".join(prefix + line for line in text.splitlines())


def wait_for_mode(vid: int, pid: int, expected_mode: int, timeout_s: float, poll_s: float) -> tuple[str, dict]:
    deadline = time.monotonic() + timeout_s
    last_message = "device not checked yet"

    while time.monotonic() < deadline:
        try:
            path, status = read_status(vid, pid)
            if status["active_mode"] == expected_mode and status["stored_mode"] == expected_mode:
                return path, status
            last_message = (
                f"active={status['active_mode']} stored={status['stored_mode']} "
                f"last_status={status['last_status']}"
            )
        except Exception as exc:
            last_message = str(exc)
        time.sleep(poll_s)

    raise TimeoutError(f"Timed out waiting for {mode_label(expected_mode)}; last state: {last_message}")


def run_powershell_json(script: str) -> object:
    wrapped = "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; " + script
    completed = subprocess.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", wrapped],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or completed.stdout.strip() or "PowerShell command failed")

    output = completed.stdout.strip()
    if not output:
        return []
    return json.loads(output)


def collect_windows_devices(vid: int) -> dict:
    vid_text = f"VID_{vid:04X}"
    script = rf"""
$vid = '{vid_text}'
$pnp = @(Get-PnpDevice -PresentOnly | Where-Object {{ $_.InstanceId -like "*$vid*" }} |
    Select-Object Status, Class, FriendlyName, InstanceId)
$serial = @(Get-CimInstance Win32_SerialPort | Where-Object {{ $_.PNPDeviceID -like "*$vid*" }} |
    Select-Object DeviceID, Description, PNPDeviceID)
$result = [ordered]@{{ Pnp = $pnp; Serial = $serial }}
ConvertTo-Json -Depth 5 -InputObject $result
"""
    result = run_powershell_json(script)
    return {
        "pnp": normalize_json_array(result.get("Pnp", [])),
        "serial": normalize_json_array(result.get("Serial", [])),
    }


def normalize_json_array(value):
    if value is None:
        return []
    if isinstance(value, list):
        return value
    return [value]


def contains_pid(text: str | None, pid: int) -> bool:
    return text is not None and f"PID_{pid:04X}" in text.upper()


def get_serial_roles(devices: dict) -> dict[str, list[dict]]:
    roles = {"host": [], "target": []}
    for item in devices["serial"]:
        pnp_id = item.get("PNPDeviceID", "")
        if contains_pid(pnp_id, 0xFE0C):
            roles["host"].append(item)
        if contains_pid(pnp_id, 0xFE00):
            roles["target"].append(item)
    return roles


def get_media_roles(devices: dict) -> dict[str, list[dict]]:
    roles = {"host": [], "target": []}
    for item in devices["pnp"]:
        class_name = str(item.get("Class") or "").lower()
        if class_name != "media" or item.get("Status") != "OK":
            continue
        instance_id = item.get("InstanceId", "")
        name = item.get("FriendlyName", "") or ""
        if contains_pid(instance_id, 0xFE0C) or "Host Audio" in name:
            roles["host"].append(item)
        if contains_pid(instance_id, 0xFE00) or "Target Audio" in name:
            roles["target"].append(item)
    return roles


def validate_enumeration(mode: int, devices: dict, strict: bool) -> tuple[bool, list[str]]:
    notes = []
    passed = True
    serial = get_serial_roles(devices)
    media = get_media_roles(devices)
    non_ok = [item for item in devices["pnp"] if item.get("Status") not in ("OK", "Unknown")]

    def require(condition: bool, message: str):
        nonlocal passed
        if condition:
            notes.append("OK: " + message)
        else:
            notes.append("FAIL: " + message)
            passed = False

    def warn(condition: bool, message: str):
        nonlocal passed
        if not condition:
            notes.append("WARN: " + message)
            if strict:
                passed = False

    if mode == 1:
        require(len(serial["host"]) >= 1, "host CDC ACM port present")
        warn(len(serial["target"]) == 0, "target CDC ACM port absent")
        warn(len(media["host"]) == 0 and len(media["target"]) == 0, "UAC media devices absent")
    elif mode == 2:
        require(len(serial["host"]) >= 1, "host CDC ACM port present")
        require(len(media["host"]) >= 1, "Host Audio media device present")
        require(len(media["target"]) >= 1, "Target Audio media device present")
        warn(len(serial["target"]) == 0, "target CDC ACM port absent")
    elif mode == 3:
        require(len(serial["host"]) >= 1, "host CDC ACM port present")
        require(len(serial["target"]) >= 1, "target CDC ACM port present")
        warn(len(media["host"]) == 0 and len(media["target"]) == 0, "UAC media devices absent")
    elif mode == 4:
        notes.append("OK: HID mode report confirms Mode 4")
        notes.append("INFO: Windows stock drivers usually do not bind CDC ECM as a Net adapter")
        warn(len(serial["host"]) == 0 and len(serial["target"]) == 0, "CDC ACM ports absent")
        warn(len(media["host"]) == 0 and len(media["target"]) == 0, "UAC media devices absent")
    else:
        require(False, f"unknown mode {mode}")

    if non_ok:
        detail = "; ".join(
            f"{item.get('Status')} {item.get('Class')} {item.get('FriendlyName')}" for item in non_ok[:5]
        )
        warn(False, f"present VID_1A86 devices with non-OK status: {detail}")
    else:
        notes.append("OK: no present VID_1A86 devices in error state")

    return passed, notes


def last_output_lines(stdout: str, stderr: str, max_lines: int = 12) -> str:
    lines = []
    if stdout.strip():
        lines.extend(stdout.strip().splitlines())
    if stderr.strip():
        lines.extend(stderr.strip().splitlines())
    if not lines:
        return ""
    return "\n".join(lines[-max_lines:])


def run_command(command: list[str], timeout_s: float) -> tuple[int, str]:
    completed = subprocess.run(
        command,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout_s,
        check=False,
    )
    return completed.returncode, last_output_lines(completed.stdout, completed.stderr)


def probe_audio_devices() -> tuple[bool, list[dict] | str]:
    try:
        import sounddevice as sd
    except Exception as exc:
        return False, f"Python sounddevice module is not available: {exc}"

    try:
        devices = []
        for index, item in enumerate(sd.query_devices()):
            devices.append(
                {
                    "index": index,
                    "name": item["name"],
                    "max_input_channels": int(item["max_input_channels"]),
                    "max_output_channels": int(item["max_output_channels"]),
                }
            )
        return True, devices
    except Exception as exc:
        return False, f"PortAudio device query failed: {exc}"


def describe_audio_candidates(devices: list[dict]) -> str:
    candidates = [
        item for item in devices
        if "audio" in item["name"].lower() or "usb" in item["name"].lower() or "openterface" in item["name"].lower()
    ]
    if not candidates:
        candidates = devices
    return "; ".join(
        f"[{item['index']}] {item['name']} in={item['max_input_channels']} out={item['max_output_channels']}"
        for item in candidates[:10]
    )


def choose_audio_device(devices: list[dict], direction: str, index: int | None, preferred_name: str) -> int:
    channel_key = "max_output_channels" if direction == "output" else "max_input_channels"
    if index is not None:
        matches = [item for item in devices if item["index"] == index]
        if not matches:
            raise RuntimeError(f"Audio {direction} index {index} was not found.")
        if matches[0][channel_key] <= 0:
            raise RuntimeError(f"Audio {direction} index {index} has no {direction} channels.")
        return index

    preferred = preferred_name.lower()
    candidates = [item for item in devices if item[channel_key] > 0]
    for item in candidates:
        if preferred and preferred in item["name"].lower():
            return int(item["index"])
    for item in candidates:
        name = item["name"].lower()
        if "openterface" in name or "usb" in name or "audio" in name:
            return int(item["index"])
    if candidates:
        return int(candidates[0]["index"])
    raise RuntimeError(f"No audio {direction} device was found.")


def run_audio_loopback(args) -> tuple[bool, list[str]]:
    ok, devices_or_message = probe_audio_devices()
    if not ok:
        return False, ["FAIL: " + str(devices_or_message)]

    devices = devices_or_message
    try:
        output_index = choose_audio_device(devices, "output", args.audio_output, args.audio_output_name)
        input_index = choose_audio_device(devices, "input", args.audio_input, args.audio_input_name)
    except RuntimeError as exc:
        return False, ["FAIL: " + str(exc), "INFO: audio candidates: " + describe_audio_candidates(devices)]

    report = Path(args.audio_report)
    if not report.is_absolute():
        report = SCRIPT_DIR.parent / report

    command = [
        sys.executable,
        str(SCRIPT_DIR / "audio_jitter_test.py"),
        "--output", str(output_index),
        "--input", str(input_index),
        "--duration", str(args.audio_duration),
        "--report", str(report),
    ]
    if args.audio_record_only:
        command.append("--record-only")
    if args.audio_silence_output:
        command.append("--silence-output")

    return_code, output = run_command(command, args.functional_timeout)
    notes = [
        f"INFO: audio loopback output index {output_index}, input index {input_index}",
        "INFO: audio diagnostic tail:\n" + output,
    ]
    if return_code == 0:
        notes.insert(0, "OK: Mode 2 audio diagnostic completed")
        return True, notes
    notes.insert(0, f"FAIL: Mode 2 audio diagnostic exited with {return_code}")
    return False, notes


def run_acm_bridge_functional(args, full: bool) -> tuple[bool, list[str]]:
    command = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", str(SCRIPT_DIR / "test_acm_bridge_windows.ps1"),
    ]
    if full:
        command.extend([
            "-BaudRates", args.acm_baud_rates,
            "-PayloadSizes", args.acm_payload_sizes,
            "-Iterations", str(args.acm_iterations),
            "-TimeoutMs", str(args.acm_timeout_ms),
        ])
    else:
        command.append("-ListPortsOnly")

    return_code, output = run_command(command, args.functional_timeout)
    label = "transfer" if full else "port discovery"
    notes = [f"INFO: Mode 3 ACM {label} tail:\n" + output]
    if return_code == 0:
        notes.insert(0, f"OK: Mode 3 ACM bridge {label} completed")
        return True, notes
    notes.insert(0, f"FAIL: Mode 3 ACM bridge {label} exited with {return_code}")
    return False, notes


def run_functional_tests(mode: int, args, devices: dict) -> tuple[bool, list[str]]:
    if args.functional_tests == "none":
        return True, ["SKIP: functional tests disabled"]

    notes = []
    passed = True
    serial = get_serial_roles(devices)
    media = get_media_roles(devices)
    full = args.functional_tests == "full"

    if mode == 1:
        try:
            _, status = read_status(args.vid, args.pid)
            if status["active_mode"] == 1 and status["stored_mode"] == 1:
                notes.append("OK: Mode 1 HID control readback works")
            else:
                notes.append(f"FAIL: Mode 1 HID readback returned active={status['active_mode']} stored={status['stored_mode']}")
                passed = False
        except Exception as exc:
            notes.append(f"FAIL: Mode 1 HID control readback failed: {exc}")
            passed = False
        if serial["host"]:
            notes.append("OK: Mode 1 host CDC ACM management port is present")
        else:
            notes.append("FAIL: Mode 1 host CDC ACM management port is missing")
            passed = False
    elif mode == 2:
        ok, audio_devices_or_message = probe_audio_devices()
        if ok:
            notes.append("OK: PortAudio can list audio devices")
            notes.append("INFO: audio candidates: " + describe_audio_candidates(audio_devices_or_message))
        else:
            notes.append("WARN: " + str(audio_devices_or_message))
            if full:
                passed = False
        if media["host"] and media["target"]:
            notes.append("OK: Mode 2 host and target UAC media devices are present")
        else:
            notes.append("FAIL: Mode 2 UAC media devices are incomplete")
            passed = False
        if full:
            audio_ok, audio_notes = run_audio_loopback(args)
            notes.extend(audio_notes)
            passed = passed and audio_ok
        else:
            notes.append("SKIP: audio loopback is only run with --functional-tests full")
    elif mode == 3:
        acm_ok, acm_notes = run_acm_bridge_functional(args, full)
        notes.extend(acm_notes)
        passed = passed and acm_ok
    elif mode == 4:
        notes.append("OK: Mode 4 HID control readback works")
        notes.append("SKIP: CDC ECM data-path functional test is not run on stock Windows")
        if args.require_ecm_net:
            net_devices = [
                item for item in devices["pnp"]
                if str(item.get("Class") or "").lower() == "net" and item.get("Status") == "OK"
            ]
            if net_devices:
                notes.append("OK: ECM/NIC device is bound by Windows")
            else:
                notes.append("FAIL: --require-ecm-net was set but no OK Net adapter was found")
                passed = False
    else:
        notes.append(f"FAIL: no functional test mapping for mode {mode}")
        passed = False

    return passed, notes


def capability_has_mode(mask: int, mode: int) -> bool:
    return (mask & (1 << mode)) != 0


def assert_status(status: dict, expected_mode: int, required_mask: int) -> list[str]:
    notes = []
    if status["active_mode"] != expected_mode:
        raise RuntimeError(f"Expected active mode {expected_mode}, got {status['active_mode']}.")
    if status["stored_mode"] != expected_mode:
        raise RuntimeError(f"Expected stored mode {expected_mode}, got {status['stored_mode']}.")
    if status["config_status"] != 1:
        raise RuntimeError(f"Expected config_status 1, got {status['config_status']}.")
    if status["last_status"] != 0:
        raise RuntimeError(f"Expected last_status 0, got {status['last_status']}.")
    if status["capability_mask"] & required_mask != required_mask:
        raise RuntimeError(
            f"Capability mask 0x{status['capability_mask']:08X} does not include 0x{required_mask:08X}."
        )
    notes.append(
        "OK: HID status active={active_mode} stored={stored_mode} compiled={compiled_mode} "
        "capability=0x{capability_mask:08X}".format(**status)
    )
    return notes


def switch_to_mode(args, mode: int, control_path: str | None = None) -> tuple[dict, list[str]]:
    if control_path is None:
        control_path = args.control_path

    print(f"\nSwitching to {mode_label(mode)}")
    _, before_status = read_status(args.vid, args.pid)
    if control_path == "ch9329":
        send_write_mode_ch9329(args, before_status["active_mode"], mode)
        send_reboot_ch9329(args)
    else:
        write_status = send_write_mode(args.vid, args.pid, mode, args.flags)
        print(
            "  HID write accepted: stored={stored_mode} active={active_mode} capability=0x{capability_mask:08X}".format(
                **write_status
            )
        )
        send_reboot(args.vid, args.pid)

    path, status = wait_for_mode(args.vid, args.pid, mode, args.timeout, args.poll)
    print(f"  re-enumerated mode HID: {path}")
    notes = assert_status(status, mode, args.required_mask)
    notes.append(f"OK: switch command path was {control_path}")

    if args.skip_enumeration:
        notes.append("SKIP: Windows enumeration check disabled")
        return status, notes

    time.sleep(args.enumeration_settle)
    devices = collect_windows_devices(args.vid)
    enum_ok, enum_notes = validate_enumeration(mode, devices, args.strict_enumeration)
    notes.extend(enum_notes)
    if not enum_ok:
        detail = "\n".join("  " + note for note in enum_notes)
        raise RuntimeError("Enumeration validation failed for " + mode_label(mode) + "\n" + detail)

    functional_ok, functional_notes = run_functional_tests(mode, args, devices)
    notes.extend(functional_notes)
    if not functional_ok:
        detail = "\n".join("  " + note for note in functional_notes)
        raise RuntimeError("Functional validation failed for " + mode_label(mode) + "\n" + detail)
    return status, notes


def ensure_mode(args, mode: int, control_path: str | None = None) -> tuple[dict, list[str]]:
    try:
        _, status = read_status(args.vid, args.pid)
        if status["active_mode"] == mode and status["stored_mode"] == mode:
            notes = assert_status(status, mode, args.required_mask)
            notes.append("OK: already in requested source mode")
            return status, notes
    except Exception:
        pass
    return switch_to_mode(args, mode, control_path)


def build_transition_plan(modes: list[int], plan: str) -> list[tuple[int | None, int]]:
    if plan == "cycle":
        return [(None, mode) for mode in modes]
    if plan == "reverse-cycle":
        return [(None, mode) for mode in reversed(modes)]

    transitions = []
    for source in modes:
        for target in modes:
            if source != target:
                transitions.append((source, target))
    return transitions


def parse_modes(text: str) -> list[int]:
    modes = []
    for part in text.split(","):
        item = part.strip()
        if not item:
            continue
        mode = int(item, 0)
        if mode not in MODE_NAMES:
            raise argparse.ArgumentTypeError(f"Invalid mode {mode}; expected one of 1,2,3,4.")
        if mode not in modes:
            modes.append(mode)
    if not modes:
        raise argparse.ArgumentTypeError("At least one mode is required.")
    return modes


def print_notes(notes: list[str], indent: str = "  ") -> None:
    for note in notes:
        print(indent + note)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Test Openterface runtime USB mode switching over HID Feature report 0x30."
    )
    parser.add_argument("--vid", type=lambda value: int(value, 0), default=0x1A86)
    parser.add_argument("--pid", type=lambda value: int(value, 0), default=0xFE00)
    parser.add_argument("--modes", type=parse_modes, default=parse_modes("1,2,3,4"))
    parser.add_argument(
        "--plan",
        choices=["matrix", "cycle", "reverse-cycle"],
        default="matrix",
        help="matrix tests every ordered source->target pair; cycle is faster.",
    )
    parser.add_argument("--flags", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--timeout", type=float, default=25.0, help="Seconds to wait for each re-enumeration.")
    parser.add_argument("--poll", type=float, default=0.5, help="Seconds between HID status polls.")
    parser.add_argument("--enumeration-settle", type=float, default=1.0)
    parser.add_argument("--required-mask", type=lambda value: int(value, 0), default=EXPECTED_CAPABILITY_MASK)
    parser.add_argument(
        "--control-path",
        choices=["hid", "ch9329"],
        default="hid",
        help="Use HID Feature report or CH9329 command 0x30 over host CDC ACM for the tested switch.",
    )
    parser.add_argument("--ch9329-port", help="host CDC ACM COM port override, usually VID_1A86 PID_FE0C")
    parser.add_argument("--ch9329-baud-rate", type=int, default=115200)
    parser.add_argument("--ch9329-timeout-ms", type=int, default=3000)
    parser.add_argument("--skip-enumeration", action="store_true")
    parser.add_argument("--strict-enumeration", action="store_true")
    parser.add_argument(
        "--functional-tests",
        choices=["none", "smoke", "full"],
        default="none",
        help="smoke adds noninvasive mode checks; full runs available data-path tests.",
    )
    parser.add_argument("--functional-timeout", type=float, default=90.0)
    parser.add_argument("--acm-baud-rates", default="9600,115200,921600")
    parser.add_argument("--acm-payload-sizes", default="16,64,255")
    parser.add_argument("--acm-iterations", type=int, default=1)
    parser.add_argument("--acm-timeout-ms", type=int, default=3000)
    parser.add_argument("--audio-output", type=int, help="audio output device index for Mode 2 full test")
    parser.add_argument("--audio-input", type=int, help="audio input device index for Mode 2 full test")
    parser.add_argument("--audio-output-name", default="Host Audio")
    parser.add_argument("--audio-input-name", default="Target Audio")
    parser.add_argument("--audio-duration", type=float, default=3.0)
    parser.add_argument("--audio-report", default="audio_jitter_report.png")
    parser.add_argument("--audio-record-only", action="store_true")
    parser.add_argument("--audio-silence-output", action="store_true")
    parser.add_argument("--require-ecm-net", action="store_true")
    parser.add_argument("--no-restore", action="store_true", help="Leave the device in the final tested mode.")
    args = parser.parse_args()

    initial_path, initial_status = read_status(args.vid, args.pid)
    initial_mode = initial_status["active_mode"]
    print(f"Initial mode HID: {initial_path}")
    print(
        "Initial status: compiled={compiled_mode} active={active_mode} stored={stored_mode} "
        "config={config_status} capability=0x{capability_mask:08X}".format(**initial_status)
    )

    missing_modes = [mode for mode in args.modes if not capability_has_mode(initial_status["capability_mask"], mode)]
    if missing_modes:
        names = ", ".join(mode_label(mode) for mode in missing_modes)
        print(f"ERROR: firmware capability mask does not advertise: {names}")
        return 2

    transitions = build_transition_plan(args.modes, args.plan)
    print(f"Test plan: {args.plan}, {len(transitions)} transition case(s)")
    print(f"Control path: {args.control_path}")

    results: list[TransitionResult] = []
    try:
        for index, (source, target) in enumerate(transitions, start=1):
            if source is None:
                current_path, current_status = read_status(args.vid, args.pid)
                source = current_status["active_mode"]
                print(f"\n[{index}/{len(transitions)}] {mode_label(source)} -> {mode_label(target)}")
            else:
                print(f"\n[{index}/{len(transitions)}] {mode_label(source)} -> {mode_label(target)}")
                source_setup_path = "hid" if args.control_path == "ch9329" else None
                _, source_notes = ensure_mode(args, source, source_setup_path)
                print_notes(source_notes)

            try:
                status, notes = switch_to_mode(args, target)
                print_notes(notes)
                results.append(TransitionResult(source, target, True, status, notes))
            except Exception as exc:
                print(f"  FAIL: {exc}")
                results.append(TransitionResult(source, target, False, None, [str(exc)]))
                break
    finally:
        if not args.no_restore:
            try:
                _, current_status = read_status(args.vid, args.pid)
                if current_status["active_mode"] != initial_mode or current_status["stored_mode"] != initial_mode:
                    print(f"\nRestoring initial {mode_label(initial_mode)}")
                    restore_path = "hid" if args.control_path == "ch9329" else None
                    _, restore_notes = switch_to_mode(args, initial_mode, restore_path)
                    print_notes(restore_notes)
            except Exception as exc:
                print(f"WARN: could not restore initial mode {initial_mode}: {exc}")

    print("\nSummary")
    print("Source -> Target : Result")
    for result in results:
        outcome = "PASS" if result.passed else "FAIL"
        print(f"{result.source} -> {result.target} : {outcome}")

    failures = [result for result in results if not result.passed]
    if failures:
        print(f"\nFAILED: {len(failures)} transition case(s) failed.")
        return 1

    print(f"\nPASSED: {len(results)} transition case(s) passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())