#!/usr/bin/env python3
"""
Test CH9329 commands via BLE serial (UART2)
This tests if the firmware is running and responding via BLE path
"""
import serial
import time
import sys

def calculate_checksum(data):
    """Calculate CH9329 checksum (sum of all bytes mod 256)"""
    return sum(data) % 256

def test_ble_serial():
    """Test BLE serial port (UART2) communication"""
    # BLE serial typically appears as a different device
    ports_to_try = [
        '/dev/cu.usbmodem211201',
        '/dev/cu.usbserial-0001',
        '/dev/cu.usbmodem0000000000001'
    ]

    baudrates = [9600, 19200, 38400, 57600, 115200]

    for port in ports_to_try:
        try:
            print(f"\nTrying {port}...")
            for baud in baudrates:
                try:
                    ser = serial.Serial(port, baud, timeout=1)
                    time.sleep(0.1)

                    # Send Get Info command
                    frame = bytes([0x57, 0xAB, 0x00, 0x01, 0x00])
                    checksum = calculate_checksum(frame)
                    frame += bytes([checksum])

                    print(f"  Testing {baud} baud: {frame.hex(' ').upper()}")
                    ser.write(frame)
                    ser.flush()
                    time.sleep(0.5)

                    if ser.in_waiting > 0:
                        resp = ser.read(ser.in_waiting)
                        print(f"  ✓ Response: {resp.hex(' ').upper()}")
                        ser.close()
                        return True

                    ser.close()

                except Exception as e:
                    print(f"  Error at {baud}: {e}")
                    continue

        except Exception as e:
            print(f"  Port {port} not available: {e}")
            continue

    return False

if __name__ == '__main__':
    print("=" * 60)
    print("BLE Serial Test")
    print("=" * 60)

    success = test_ble_serial()

    if success:
        print(f"\n{'=' * 60}")
        print("✓ Communication successful!")
        print(f"{'=' * 60}")
        sys.exit(0)
    else:
        print(f"\n{'=' * 60}")
        print("✗ No response from any port")
        print(f"{'=' * 60}")
        sys.exit(1)
