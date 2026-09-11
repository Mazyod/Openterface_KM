#!/usr/bin/env python3
import serial
import time
import sys

def send_ch9329_command(port='/dev/cu.usbmodem211201', baudrate=9600):
    """Send CH9329 Get Info command (0x01) to read firmware version"""
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        time.sleep(0.1)

        # CH9329 Get Info command frame
        # Frame format: 0x57 0xAB <address> <command> <data_len> <data> <checksum>
        # Get Info (0x01) has no data
        frame = bytes([0x57, 0xAB, 0x00, 0x01, 0x00])

        # Calculate checksum (sum of all bytes mod 256)
        checksum = sum(frame) & 0xFF
        frame += bytes([checksum])

        print(f"Sending CH9329 Get Info command to {port} at {baudrate} baud")
        print(f"Command frame: {frame.hex(' ').upper()}")

        ser.write(frame)
        ser.flush()

        # Wait for response
        time.sleep(0.1)

        # Read response (should be at least 7 bytes for Get Info response)
        if ser.in_waiting > 0:
            response = ser.read(ser.in_waiting)
            print(f"Received {len(response)} bytes")
            print(f"Response frame: {response.hex(' ').upper()}")

            # Parse Get Info response
            # Response format: 0x57 0xAB <address> <cmd|0x80> <data_len> <data> <checksum>
            # Get Info response should have 8 bytes of data
            if len(response) >= 13:  # 5 header + 8 data
                if response[0] == 0x57 and response[1] == 0xAB:
                    cmd = response[3]
                    data_len = response[4]
                    data = response[5:5+data_len]

                    if cmd == 0x81 and data_len == 8:
                        print("\n✓ Successfully parsed Get Info response!")
                        print(f"  Hardware version: {data[0]}")
                        print(f"  Connection status: {data[1]}")
                        print(f"  LED status: {data[2]:02X} (NumLock={data[2]&1}, CapsLock={(data[2]>>1)&1}, ScrollLock={(data[2]>>2)&1})")
                        print(f"  Firmware version: {data[3]}")
                        print(f"  Reserved bytes: {data[4:8].hex(' ')}")

                        ser.close()
                        return data[3]  # Return firmware version

            print("\n✗ Response format doesn't match expected Get Info response")
        else:
            print("✗ No response received (timeout)")

        ser.close()

    except Exception as e:
        print(f"Error: {e}")
        return None

    return None

if __name__ == '__main__':
    print("=" * 60)
    print("CH9329 Firmware Version Check")
    print("=" * 60)
    print()

    baudrates = [9600, 19200, 38400, 57600, 115200]

    for baud in baudrates:
        print(f"\n--- Trying {baud} baud ---")
        version = send_ch9329_command('/dev/cu.usbmodem211201', baud)

        if version is not None:
            print(f"\n{'=' * 60}")
            print(f"✓ Firmware version: {version}")
            print(f"{'=' * 60}")
            sys.exit(0)

        time.sleep(0.5)

    print(f"\n{'=' * 60}")
    print("✗ Failed to get firmware version at any baud rate")
    print(f"{'=' * 60}")
    sys.exit(1)