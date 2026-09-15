#!/usr/bin/env python3
import serial
import time
import sys

# Open serial port
port = '/dev/cu.usbmodem211201'
baud = 9600

try:
    ser = serial.Serial(port, baud, timeout=2)
    print(f"Opened {port} at {baud} baud")
except Exception as e:
    print(f"Failed to open port: {e}")
    sys.exit(1)

print("Sending test data... Watch for LED blink")
print("Press Ctrl+C to stop")

try:
    while True:
        # Send simple data
        test_data = bytes([0x57, 0xAB, 0x00, 0x01, 0x00, 0x03])
        print(f"Sending: {test_data.hex()}")
        ser.write(test_data)
        ser.flush()
        time.sleep(1)

        # Try to read response
        if ser.in_waiting > 0:
            response = ser.read(ser.in_waiting)
            print(f"Received: {response.hex()}")
        else:
            print("No response")

except KeyboardInterrupt:
    print("\nStopped")

ser.close()
