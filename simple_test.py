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

print("Sending 01 command once...")
cmd = bytes([0x57, 0xAB, 0x00, 0x01, 0x00, 0x03])
print(f"Sending: {cmd.hex()}")
ser.write(cmd)
ser.flush()

# Wait a bit
time.sleep(0.5)

# Try to read response
if ser.in_waiting > 0:
    response = ser.read(ser.in_waiting)
    print(f"Received: {response.hex()}")
else:
    print("No response")

ser.close()