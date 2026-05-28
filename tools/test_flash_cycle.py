#!/usr/bin/env python3
"""
Auto build & flash cycle test for Openterface CH32 devices.

Test sequence:
1. Power off target CH32
2. Pull up flash button (enter ISP mode)
3. Power on target CH32
4. Flash firmware using wchisp_c
5. Pull down flash button (run mode)
6. Verify target is running
"""

import serial
import subprocess
import sys
import time
import os

# Configuration
CONTROL_DEV = "/dev/cu.usbmodem212101"  # Control CH32 serial port
BAUDRATE = 115200
FIRMWARE_BIN = "build/openterface-kvmgo-from-artifact.bin"
WCHISP_TOOL = "tools/wchisp_c"

# Protocol commands (hex bytes)
# Flash button: 0x19
CMD_FLASH_UP   = bytes.fromhex("57 AB 00 19 01 01 1D")  # Enter flash mode
CMD_FLASH_DOWN = bytes.fromhex("57 AB 00 19 01 00 1C")  # Run mode
# Power control: 0x1A
CMD_POWER_ON   = bytes.fromhex("57 AB 00 1A 01 01 1E")  # Power on
CMD_POWER_OFF  = bytes.fromhex("57 AB 00 1A 01 00 1D")  # Power off

def send_cmd(ser, cmd, label, wait_ms=500):
    """Send a command and wait for ACK."""
    print(f"  [{label}] Sending: {cmd.hex()}")
    ser.write(cmd)
    ser.flush()
    time.sleep(wait_ms / 1000.0)

    # Read ACK response
    resp = ser.read(64)
    if resp:
        print(f"  [{label}] ACK: {resp.hex()}")
    else:
        print(f"  [{label}] No ACK received (non-critical)")
    return resp

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)

    firmware_path = os.path.join(root, FIRMWARE_BIN)
    wchisp_path = os.path.join(root, WCHISP_TOOL)

    print("=" * 60)
    print("Auto Build & Flash Cycle Test")
    print("=" * 60)

    # Check prerequisites
    if not os.path.exists(firmware_path):
        print(f"ERROR: Firmware binary not found: {firmware_path}")
        sys.exit(1)
    print(f"Firmware: {firmware_path} ({os.path.getsize(firmware_path)} bytes)")

    if not os.path.exists(wchisp_path):
        print(f"ERROR: wchisp_c tool not found: {wchisp_path}")
        sys.exit(1)

    # Step 1: Open serial connection to control CH32
    print(f"\n[Step 1] Opening serial connection to {CONTROL_DEV}")
    try:
        ser = serial.Serial(CONTROL_DEV, baudrate=BAUDRATE, timeout=1)
        print(f"  Connected to {CONTROL_DEV}")
    except Exception as e:
        print(f"  ERROR: Could not open {CONTROL_DEV}: {e}")
        sys.exit(1)

    try:
        # Step 2: Power off target
        print(f"\n[Step 2] Powering off target CH32")
        send_cmd(ser, CMD_POWER_OFF, "POWER_OFF", wait_ms=500)
        time.sleep(0.5)

        # Step 3: Pull up flash button (enter ISP mode)
        print(f"\n[Step 3] Pulling up flash button (enter ISP mode)")
        send_cmd(ser, CMD_FLASH_UP, "FLASH_UP", wait_ms=500)
        time.sleep(0.3)

        # Step 4: Power on target
        print(f"\n[Step 4] Powering on target CH32")
        send_cmd(ser, CMD_POWER_ON, "POWER_ON", wait_ms=1000)
        time.sleep(1.0)  # Wait for target to boot into ISP mode

        # Step 5: Flash the firmware
        print(f"\n[Step 5] Flashing firmware with wchisp_c")
        flash_cmd = [wchisp_path, "flash", firmware_path]
        print(f"  Running: {' '.join(flash_cmd)}")

        flash_start = time.time()
        try:
            result = subprocess.run(
                flash_cmd,
                capture_output=True,
                text=True,
                timeout=60
            )
            flash_duration = time.time() - flash_start

            if result.returncode == 0:
                print(f"  Flash SUCCESS ({flash_duration:.1f}s)")
                if result.stdout:
                    for line in result.stdout.strip().split('\n'):
                        print(f"    {line}")
            else:
                print(f"  Flash FAILED (exit code {result.returncode}, {flash_duration:.1f}s)")
                if result.stderr:
                    for line in result.stderr.strip().split('\n'):
                        print(f"    ERR: {line}")
                if result.stdout:
                    for line in result.stdout.strip().split('\n'):
                        print(f"    OUT: {line}")
        except subprocess.TimeoutExpired:
            print(f"  Flash TIMED OUT after 60s")
            # Continue anyway to complete the cycle

        # Step 6: Pull down flash button (run mode)
        print(f"\n[Step 6] Pulling down flash button (run mode)")
        send_cmd(ser, CMD_FLASH_DOWN, "FLASH_DOWN", wait_ms=500)
        time.sleep(1.0)

        # Step 7: Verify target is running
        print(f"\n[Step 7] Verifying target device")
        print("  Checking USB device list...")
        result = subprocess.run(
            ["python3", "-c", """
import usb.core
dev = usb.core.find(idVendor=0x1a86, idProduct=0xfe00)
if dev:
    print(f"  FOUND: {dev.manufacturer} {dev.product} (SN: {dev.serial_number})")
else:
    print("  NOT FOUND: Target device not detected")
"""],
            capture_output=True, text=True, timeout=10
        )
        print(result.stdout.strip())
        if result.stderr:
            print(f"  stderr: {result.stderr.strip()}")

        print("\n" + "=" * 60)
        print("Flash cycle test COMPLETE")
        print("=" * 60)

    finally:
        ser.close()
        print(f"\nSerial connection closed: {CONTROL_DEV}")

if __name__ == "__main__":
    main()
