#!/usr/bin/env python3
import serial
import time
import sys

# CH9329 Get Info command
# Frame: 57 AB 00 01 00 <checksum>
# Checksum = (0x57 + 0xAB + 0x00 + 0x01 + 0x00) % 256 = 0x03

def calculate_checksum(data):
    """Calculate CH9329 checksum (sum of all bytes mod 256)"""
    return sum(data) % 256

def send_get_info_command(port='/dev/tty.usbmodem211201', baudrate=9600):
    try:
        # Open serial port
        ser = serial.Serial(port, baudrate, timeout=0.5)
        time.sleep(0.2)  # Brief pause for serial port to settle

        # Build Get Info command
        cmd = bytes([0x57, 0xAB, 0x00, 0x01, 0x00])
        checksum = calculate_checksum(cmd)
        full_cmd = cmd + bytes([checksum])

        print(f"[{baudrate} baud] Sending Get Info command: {full_cmd.hex().upper()}")

        # Send command
        ser.write(full_cmd)
        ser.flush()  # Ensure all data is sent

        # Wait for response
        time.sleep(0.2)

        # Read response
        response = ser.read(64)

        if not response:
            print(f"[{baudrate} baud] No response received")
            ser.close()
            return False

        print(f"[{baudrate} baud] Response: {response.hex().upper()}")

        # Parse response
        # Expected format: 57 AB <addr> 81 <len> <payload...> <checksum>
        if len(response) >= 13 and response[0] == 0x57 and response[1] == 0xAB:
            addr = response[2]
            cmd_resp = response[3]
            payload_len = response[4]
            payload = response[5:5+payload_len]
            checksum_recv = response[5+payload_len]

            # Verify checksum
            checksum_calc = calculate_checksum(response[:5+payload_len])

            if checksum_recv == checksum_calc:
                print(f"[{baudrate} baud] ✓ Valid response (checksum OK)")
                print(f"[{baudrate} baud]   Address: 0x{addr:02X}")
                print(f"[{baudrate} baud]   Command response: 0x{cmd_resp:02X}")
                print(f"[{baudrate} baud]   Payload length: {payload_len} bytes")

                if payload_len >= 8:
                    hardware_version = payload[0]
                    connection_status = payload[1]
                    led_status = payload[2]
                    firmware_version = payload[3]

                    print(f"\n📊 Device Information ({baudrate} baud):")
                    print(f"  Hardware version: {hardware_version} (0x{hardware_version:02X})")
                    print(f"  Connection status: {'Connected' if connection_status == 0x01 else 'Not connected'} (0x{connection_status:02X})")
                    print(f"  LED status: 0x{led_status:02X}")
                    print(f"    - Num Lock: {'ON' if led_status & 0x01 else 'OFF'}")
                    print(f"    - Caps Lock: {'ON' if led_status & 0x02 else 'OFF'}")
                    print(f"    - Scroll Lock: {'ON' if led_status & 0x04 else 'OFF'}")
                    print(f"  Firmware version: {firmware_version} (0x{firmware_version:02X})")

                    # Check if this is the expected version
                    if firmware_version == 0x05:
                        print(f"\n✅ Firmware version confirmed: 0x{firmware_version:02X} (latest)")
                    else:
                        print(f"\n⚠️  Firmware version: 0x{firmware_version:02X} (expected 0x05)")

                    ser.close()
                    return True
                else:
                    print(f"[{baudrate} baud]   Payload too short: {payload.hex()}")
            else:
                print(f"[{baudrate} baud]   ✗ Checksum mismatch: received 0x{checksum_recv:02X}, calculated 0x{checksum_calc:02X}")
        else:
            print(f"[{baudrate} baud]   ✗ Invalid response format")

        ser.close()
        return False

    except serial.SerialException as e:
        print(f"[{baudrate} baud] Serial port error: {e}")
        return False
    except Exception as e:
        print(f"[{baudrate} baud] Error: {e}")
        return False

def main():
    port = '/dev/tty.usbmodem211201'
    baudrates = [9600, 19200, 38400, 57600, 115200]

    print(f"Checking firmware version on {port}...")
    print("=" * 50)

    for baud in baudrates:
        if send_get_info_command(port, baud):
            print("\n" + "=" * 50)
            print("✅ Success! Firmware version checked.")
            return 0
        print()

    print("=" * 50)
    print("❌ No response received at any baud rate.")
    return 1

if __name__ == '__main__':
    sys.exit(main())