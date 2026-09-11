#!/usr/bin/env python3
import serial
import time
import sys

def calculate_checksum(data):
    """Calculate CH9329 checksum (sum of all bytes mod 256)"""
    return sum(data) % 256

def send_usb_work_mode_query(port='/dev/cu.usbmodem211201', baudrate=115200):
    """Send USB Work Mode query command (0x30) to check current mode"""
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        time.sleep(0.2)

        # USB Work Mode query command
        # Frame: 57 AB 00 30 01 00 <checksum>
        # pdata[0] = 0x00 means query current mode
        frame = bytes([0x57, 0xAB, 0x00, 0x30, 0x01, 0x00])
        checksum = calculate_checksum(frame)
        frame += bytes([checksum])

        print(f"Sending USB Work Mode query to {port} at {baudrate} baud")
        print(f"Command frame: {frame.hex(' ').upper()}")

        ser.write(frame)
        ser.flush()

        # Wait for response
        time.sleep(0.5)

        # Read response
        if ser.in_waiting > 0:
            response = ser.read(ser.in_waiting)
            print(f"Received {len(response)} bytes")
            print(f"Response frame: {response.hex(' ').upper()}")

            # Parse USB Work Mode response
            # Response format: 57 AB <addr> <cmd|0x80> <len> <data> <checksum>
            if len(response) >= 7 and response[0] == 0x57 and response[1] == 0xAB:
                cmd = response[3]
                data_len = response[4]
                data = response[5:5+data_len]

                if cmd == 0xB0 and data_len >= 1:  # 0x30 | 0x80 = 0xB0
                    print("\n✓ Successfully parsed USB Work Mode response!")
                    print(f"  Status: 0x{data[0]:02X}")
                    if len(data) >= 4:
                        mode_names = {
                            1: "HID_ACM",
                            2: "HID_UAC_ACM",
                            3: "HID_ACM_BRIDGE",
                            4: "HID_ECM_BRIDGE",
                            5: "HID_UAC_ECM",
                            6: "HID_ACM_ECM"
                        }
                        compiled_mode = data[1]
                        active_mode = data[2]
                        stored_mode = data[3]

                        print(f"  Compiled mode: {compiled_mode} ({mode_names.get(compiled_mode, 'Unknown')})")
                        print(f"  Active mode: {active_mode} ({mode_names.get(active_mode, 'Unknown')})")
                        print(f"  Stored mode: {stored_mode} ({mode_names.get(stored_mode, 'Unknown')})")

                        ser.close()
                        return active_mode

            print("\n✗ Response format doesn't match expected USB Work Mode response")
        else:
            print("✗ No response received (timeout)")

        ser.close()

    except Exception as e:
        print(f"Error: {e}")
        return None

    return None

if __name__ == '__main__':
    print("=" * 60)
    print("USB Work Mode Query")
    print("=" * 60)
    print()

    baudrates = [115200, 9600, 19200, 38400, 57600]

    for baud in baudrates:
        print(f"\n--- Trying {baud} baud ---")
        mode = send_usb_work_mode_query('/dev/cu.usbmodem211201', baud)

        if mode is not None:
            print(f"\n{'=' * 60}")
            print(f"✓ Current USB Work Mode: {mode}")
            print(f"{'=' * 60}")
            sys.exit(0)

        time.sleep(0.5)

    print(f"\n{'=' * 60}")
    print("✗ Failed to query USB Work Mode at any baud rate")
    print(f"{'=' * 60}")
    sys.exit(1)
