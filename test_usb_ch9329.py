#!/usr/bin/env python3
import usb.core
import usb.util
import time
import sys

# CH9329 command codes
CMD_GET_ECM_IP_CONFIG = 0x27
CMD_SET_ECM_IP_CONFIG = 0x26

# Sub-commands
SUBCMD_GET_ALL = 0x00
SUBCMD_GET_HOST_SIDE = 0x01
SUBCMD_GET_TARGET_SIDE = 0x02

def calculate_checksum(data):
    """Calculate checksum (sum of all bytes)"""
    return sum(data) & 0xFF

def build_command(addr, cmd_code, data):
    """Build CH9329 command frame"""
    frame = [0x57, 0xAB, addr, cmd_code, len(data)] + data
    checksum = calculate_checksum(frame)
    frame.append(checksum)
    return bytes(frame)

def find_device():
    """Find the Openterface device"""
    # Vendor ID and Product ID from PyUSB listing
    # Note: PyUSB shows vendor=0x345F, product=0x2132
    # (ioreg showed 13407=0x346F but PyUSB shows 0x345F - using PyUSB value)
    dev = usb.core.find(idVendor=0x345F, idProduct=0x2132)

    if dev is None:
        raise ValueError('Device not found')

    return dev

def send_ch9329_command(dev, command_bytes):
    """Send a CH9329 command to the device and read response"""
    try:
        # Detach kernel driver if necessary
        if dev.is_kernel_driver_active(0):
            try:
                dev.detach_kernel_driver(0)
            except usb.core.USBError as e:
                print(f"Could not detach kernel driver: {e}")
                return None

        # Set the active configuration
        dev.set_configuration()

        # Get an endpoint instance
        cfg = dev.get_active_configuration()
        intf = cfg[(0, 0)]  # Interface 0, alternate setting 0

        # Find the OUT endpoint for sending data
        ep_out = usb.util.find_descriptor(
            intf,
            # Match the first OUT endpoint
            custom_match = lambda e: \
                usb.util.endpoint_direction(e.bEndpointAddress) == \
                usb.util.ENDPOINT_OUT
        )

        # Find the IN endpoint for receiving data
        ep_in = usb.util.find_descriptor(
            intf,
            # Match the first IN endpoint
            custom_match = lambda e: \
                usb.util.endpoint_direction(e.bEndpointAddress) == \
                usb.util.ENDPOINT_IN
        )

        if ep_out is None or ep_in is None:
            print("Could not find suitable endpoints")
            return None

        # Send the command
        print(f"Sending {len(command_bytes)} bytes: {command_bytes.hex()}")
        ep_out.write(command_bytes, timeout=1000)

        # Read response (with timeout)
        time.sleep(0.1)  # Give device time to process
        try:
            response = ep_in.read(64, timeout=1000)  # Read up to 64 bytes
            print(f"Received {len(response)} bytes: {bytes(response).hex()}")
            return bytes(response)
        except usb.core.USBError as e:
            if e.errno == 110:  # Timeout
                print("Timeout waiting for response")
                return None
            else:
                raise

    except Exception as e:
        print(f"Error: {e}")
        return None
    finally:
        # Release the interface
        usb.util.dispose_resources(dev)

def main():
    print("Testing CH9329 communication with Openterface device...")

    try:
        dev = find_device()
        print(f"Found device: {dev}")
    except ValueError as e:
        print(f"Error: {e}")
        return 1

    # Test GET ECM IP CONFIG (All)
    print("\n=== Testing GET ECM IP CONFIG (All) ===")
    cmd = build_command(0x00, CMD_GET_ECM_IP_CONFIG, [SUBCMD_GET_ALL])
    response = send_ch9329_command(dev, cmd)

    if response is None:
        print("Failed to get response")
        return 1

    # Parse response (simplified)
    if len(response) >= 6 and response[0] == 0x57 and response[1] == 0xAB:
        addr = response[2]
        cmd_code = response[3]
        data_len = response[4]
        if len(response) >= 5 + data_len + 1:
            data = response[5:5+data_len]
            checksum = response[5+data_len]
            expected_checksum = calculate_checksum(response[:5+data_len])
            if checksum == expected_checksum:
                print(f"Valid response received!")
                print(f"  Address: 0x{addr:02X}")
                print(f"  Command: 0x{cmd_code:02X}")
                print(f"  Data length: {data_len}")
                print(f"  Data: {data.hex()}")
                if len(data) >= 1:
                    status = data[0]
                    print(f"  Status: 0x{status:02X} ({'Success' if status == 0 else 'Error'})")
                if len(data) >= 21:  # Full config
                    # Parse IP addresses (simplified)
                    try:
                        host_ip = '.'.join(str(b) for b in data[1:5])
                        dev_ip = '.'.join(str(b) for b in data[5:9])
                        subnet = '.'.join(str(b) for b in data[9:13])
                        target_host_ip = '.'.join(str(b) for b in data[13:17])
                        target_dev_ip = '.'.join(str(b) for b in data[17:21])
                        print(f"  Host IP: {host_ip}")
                        print(f"  Device IP: {dev_ip}")
                        print(f"  Subnet Mask: {subnet}")
                        print(f"  Target Host IP: {target_host_ip}")
                        print(f"  Target Device IP: {target_dev_ip}")
                    except:
                        print("  Could not parse IP addresses from data")
            else:
                print(f"Checksum mismatch: got 0x{checksum:02X}, expected 0x{expected_checksum:02X}")
        else:
            print("Response too short")
    else:
        print("Invalid response format")

    return 0

if __name__ == '__main__':
    sys.exit(main())