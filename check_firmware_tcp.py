#!/usr/bin/env python3
import socket
import time
import sys

def calculate_checksum(data):
    """Calculate CH9329 checksum (sum of all bytes mod 256)"""
    return sum(data) % 256

def send_ch9329_command_tcp(host='192.168.11.3', port=20000):
    """Send CH9329 Get Info command via TCP to ECM bridge"""
    try:
        # Create TCP socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2.0)

        print(f"Connecting to {host}:{port}...")
        sock.connect((host, port))
        print("Connected!")

        # CH9329 Get Info command frame
        # Frame format: 0x57 0xAB <address> <command> <data_len> <data> <checksum>
        frame = bytes([0x57, 0xAB, 0x00, 0x01, 0x00])
        checksum = calculate_checksum(frame)
        frame += bytes([checksum])

        print(f"Sending CH9329 Get Info command: {frame.hex(' ').upper()}")
        sock.sendall(frame)

        # Wait for response
        time.sleep(0.5)

        # Read response
        response = sock.recv(1024)

        if not response:
            print("No response received")
            sock.close()
            return None

        print(f"Received {len(response)} bytes")
        print(f"Response frame: {response.hex(' ').upper()}")

        # Parse Get Info response
        if len(response) >= 13 and response[0] == 0x57 and response[1] == 0xAB:
            cmd = response[3]
            data_len = response[4]
            data = response[5:5+data_len]

            if cmd == 0x81 and data_len == 8:
                print("\n✓ Successfully parsed Get Info response!")
                print(f"  Hardware version: {data[0]}")
                print(f"  Connection status: {'Connected' if data[1] == 0x01 else 'Not connected'}")
                print(f"  LED status: 0x{data[2]:02X}")
                print(f"    - Num Lock: {'ON' if data[2] & 0x01 else 'OFF'}")
                print(f"    - Caps Lock: {'ON' if data[2] & 0x02 else 'OFF'}")
                print(f"    - Scroll Lock: {'ON' if data[2] & 0x04 else 'OFF'}")
                print(f"  Firmware version: {data[3]} (0x{data[3]:02X})")

                sock.close()
                return data[3]

        print("Response format doesn't match expected Get Info response")
        sock.close()

    except socket.timeout:
        print("Connection timed out")
        return None
    except ConnectionRefusedError:
        print("Connection refused")
        return None
    except Exception as e:
        print(f"Error: {e}")
        return None

    return None

if __name__ == '__main__':
    print("=" * 60)
    print("CH9329 Firmware Version Check via TCP (ECM Bridge)")
    print("=" * 60)
    print()

    version = send_ch9329_command_tcp('192.168.11.3', 20000)

    if version is not None:
        print(f"\n{'=' * 60}")
        print(f"✓ Firmware version: {version} (0x{version:02X})")
        if version == 0x05:
            print("✓ This is the latest firmware version!")
        print(f"{'=' * 60}")
        sys.exit(0)
    else:
        print(f"\n{'=' * 60}")
        print("✗ Failed to get firmware version")
        print(f"{'=' * 60}")
        sys.exit(1)
