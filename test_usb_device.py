#!/usr/bin/env python3
"""
Test USB control requests to verify firmware is running
"""
import struct
import ctypes
import ctypes.util
import sys

def test_usb_device():
    """Try to communicate with the device via libusb"""
    try:
        import usb.core
        import usb.util

        # WCH CH9329 vendor/product
        dev = usb.core.find(idVendor=0x1a86, idProduct=0xfe0c)
        if dev is None:
            print("Device not found")
            return False

        print(f"Found device: {dev.idVendor:04x}:{dev.idProduct:04x}")

        # Get device descriptor
        print(f"  Manufacturer: {usb.util.get_string(dev, dev.iManufacturer)}")
        print(f"  Product: {usb.util.get_string(dev, dev.iProduct)}")
        print(f"  Serial: {usb.util.get_string(dev, dev.iSerialNumber)}")

        # Try to get configuration
        for cfg in dev:
            print(f"  Configuration {cfg.bConfigurationValue}")
            for intf in cfg:
                print(f"    Interface {intf.bInterfaceNumber}: Class={intf.bInterfaceClass} SubClass={intf.bInterfaceSubClass}")
                for ep in intf:
                    print(f"      Endpoint {ep.bEndpointAddress:02x}: {ep.type}")

        return True

    except ImportError:
        print("pyusb not installed, trying alternative method...")
        return test_via_ioreg()
    except Exception as e:
        print(f"Error: {e}")
        return False

def test_via_ioreg():
    """Get device info via ioreg"""
    import subprocess

    result = subprocess.run(['ioreg', '-rc', 'IOUSBHostDevice'],
                           capture_output=True, text=True)

    if result.returncode != 0:
        print("ioreg failed")
        return False

    # Find our device
    lines = result.stdout.split('\n')
    found_device = False
    for i, line in enumerate(lines):
        if 'idVendor" = 6790' in line:
            found_device = True
            # Print surrounding context
            for j in range(max(0, i-5), min(len(lines), i+30)):
                print(lines[j])
            break

    return found_device

if __name__ == '__main__':
    print("=" * 60)
    print("USB Device Test")
    print("=" * 60)

    success = test_usb_device()

    if success:
        print(f"\n{'=' * 60}")
        print("✓ Device found")
        print(f"{'=' * 60}")
        sys.exit(0)
    else:
        print(f"\n{'=' * 60}")
        print("✗ Device not found")
        print(f"{'=' * 60}")
        sys.exit(1)
