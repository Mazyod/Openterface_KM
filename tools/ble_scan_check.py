#!/usr/bin/env python3
"""BLE scanner to check device names."""
import asyncio
from bleak import BleakScanner

async def scan_ble():
    print("Scanning for BLE devices (15 seconds)...")
    devices = await BleakScanner.discover(timeout=15.0, return_adv=True)

    print(f"\nFound {len(devices)} devices:\n")
    for addr, (device, adv_data) in devices.items():
        local_name = adv_data.local_name
        device_name = device.name
        name = local_name or device_name or "Unknown"

        # Only show devices with names we care about
        if any(x in name for x in ['KVM', 'KeyMod', 'ZZZ', 'M233']):
            rssi = adv_data.rssi
            print(f"  Name: {name:<20} | Address: {addr} | RSSI: {rssi} dBm")
            if adv_data.manufacturer_data:
                print(f"    Manufacturer data: {adv_data.manufacturer_data}")
            if adv_data.service_data:
                print(f"    Service data: {adv_data.service_data}")

if __name__ == "__main__":
    asyncio.run(scan_ble())
