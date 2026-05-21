#!/usr/bin/env python3
"""Debug HID feature report communication."""
import sys
import time
from test_usb_mode_hid_windows import (
    iter_hid_paths, open_hid, get_attrs, get_feature,
    set_feature, decode_report, INVALID_HANDLE_VALUE,
    kernel32, REPORT_ID,
)

VID = 0x1A86
PID = 0xFE00

def find_device(vid, pid):
    for path in iter_hid_paths():
        handle = open_hid(path)
        if handle == INVALID_HANDLE_VALUE:
            continue
        try:
            attrs = get_attrs(handle)
            if attrs.VendorID != vid or attrs.ProductID != pid:
                continue
            report = get_feature(handle)
            if report is None or report[0] != REPORT_ID:
                continue
            return path, handle
        except Exception:
            pass
        kernel32.CloseHandle(handle)
    return None, None

path, handle = find_device(VID, PID)
if not handle:
    print("Device not found")
    sys.exit(1)

print(f"Device: {path}")

# Step 1: Initial read
report = get_feature(handle)
if report:
    status = decode_report(report)
    print(f"Initial: last_status={status['last_status']} (0x{status['last_status']:02X})")
    print(f"  compiled_mode={status['compiled_mode']}")
    print(f"  active_mode={status['active_mode']}")
    print(f"  stored_mode={status['stored_mode']}")
    print(f"  capability_mask=0x{status['capability_mask']:08X}")

# Step 2: Send a minimal SET_REPORT (op 0x00 = read/no-op)
print("\nSending SET_REPORT op=0x00 (no-op)...")
try:
    set_feature(handle, 0x00)
    print("  SET_REPORT succeeded")
except Exception as e:
    print(f"  SET_REPORT failed: {e}")

time.sleep(0.2)

# Step 3: Read back
report = get_feature(handle)
if report:
    raw = bytes(report)
    print(f"  Raw bytes [0:8]: {raw[0:8].hex(' ')}")
    status = decode_report(report)
    print(f"  last_status={status['last_status']} (0x{status['last_status']:02X})")
    print(f"  stored_mode={status['stored_mode']}")

# Step 4: Send SET_REPORT op=0x01 with mode 1
print("\nSending SET_REPORT op=0x01 mode=1 flags=0...")
try:
    set_feature(handle, 0x01, 1, 0)
    print("  SET_REPORT succeeded")
except Exception as e:
    print(f"  SET_REPORT failed: {e}")

time.sleep(0.2)

# Step 5: Read back
report = get_feature(handle)
if report:
    raw = bytes(report)
    print(f"  Raw bytes [0:8]: {raw[0:8].hex(' ')}")
    status = decode_report(report)
    print(f"  last_status={status['last_status']} (0x{status['last_status']:02X})")
    print(f"  stored_mode={status['stored_mode']}")
