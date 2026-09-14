#!/usr/bin/env python3
import serial
import time
import sys

def calculate_checksum(data):
    """Calculate CH9329 checksum (sum of all bytes mod 256)"""
    return sum(data) % 256

def test_serial_port(port, baudrate, timeout=2):
    """Test serial port communication"""
    try:
        ser = serial.Serial(port, baudrate, timeout=timeout)
        time.sleep(0.1)

        # Clear buffers
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        # Check if there's any pending data
        if ser.in_waiting > 0:
            pending = ser.read(ser.in_waiting)
            print(f"  Pending data: {pending.hex(' ').upper()}")

        # Test 1: Send Get Info command (0x01)
        print(f"\n  Test 1: Get Info (0x01)")
        frame1 = bytes([0x57, 0xAB, 0x00, 0x01, 0x00])
        checksum1 = calculate_checksum(frame1)
        frame1 += bytes([checksum1])

        print(f"  Sending: {frame1.hex(' ').upper()}")
        ser.write(frame1)
        ser.flush()
        time.sleep(0.5)

        if ser.in_waiting > 0:
            resp1 = ser.read(ser.in_waiting)
            print(f"  Received: {resp1.hex(' ').upper()}")
            if len(resp1) >= 13 and resp1[0] == 0x57 and resp1[1] == 0xAB:
                cmd = resp1[3]
                data_len = resp1[4]
                data = resp1[5:5+data_len]
                if cmd == 0x81 and data_len == 8:
                    print(f"  ✓ Firmware version: {data[3]} (0x{data[3]:02X})")
                    ser.close()
                    return True

        # Test 2: Send USB Work Mode query (0x30)
        print(f"\n  Test 2: USB Work Mode (0x30)")
        frame2 = bytes([0x57, 0xAB, 0x00, 0x30, 0x01, 0x00])
        checksum2 = calculate_checksum(frame2)
        frame2 += bytes([checksum2])

        print(f"  Sending: {frame2.hex(' ').upper()}")
        ser.write(frame2)
        ser.flush()
        time.sleep(0.5)

        if ser.in_waiting > 0:
            resp2 = ser.read(ser.in_waiting)
            print(f"  Received: {resp2.hex(' ').upper()}")

        # Test 3: Send invalid command to see if we get error response
        print(f"\n  Test 3: Invalid command (0xFF)")
        frame3 = bytes([0x57, 0xAB, 0x00, 0xFF, 0x00])
        checksum3 = calculate_checksum(frame3)
        frame3 += bytes([checksum3])

        print(f"  Sending: {frame3.hex(' ').upper()}")
        ser.write(frame3)
        ser.flush()
        time.sleep(0.5)

        if ser.in_waiting > 0:
            resp3 = ser.read(ser.in_waiting)
            print(f"  Received: {resp3.hex(' ').upper()}")

        # Check for any spontaneous data
        print(f"\n  Checking for spontaneous data...")
        time.sleep(1)
        if ser.in_waiting > 0:
            sp_data = ser.read(ser.in_waiting)
            print(f"  Spontaneous data: {sp_data.hex(' ').upper()}")
        else:
            print(f"  No spontaneous data")

        ser.close()
        return False

    except Exception as e:
        print(f"  Error: {e}")
        return False

if __name__ == '__main__':
    print("=" * 60)
    print("CH9329 Comprehensive Test")
    print("=" * 60)

    port = '/dev/cu.usbmodem211201'
    baudrates = [115200, 9600, 19200, 38400, 57600]

    for baud in baudrates:
        print(f"\n--- Testing {baud} baud on {port} ---")
        success = test_serial_port(port, baud)

        if success:
            print(f"\n{'=' * 60}")
            print(f"✓ SUCCESS at {baud} baud!")
            print(f"{'=' * 60}")
            sys.exit(0)

        time.sleep(0.5)

    print(f"\n{'=' * 60}")
    print("✗ Failed to communicate at any baud rate")
    print(f"{'=' * 60}")
    sys.exit(1)
