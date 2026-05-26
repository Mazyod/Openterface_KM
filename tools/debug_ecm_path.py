"""Diagnostic: test basic ECM data path - just CONNECT, no DATA."""
import asyncio
from bleak import BleakClient, BleakScanner

BLE_ADDR = "DC:32:62:00:A9:77"
TX_CHAR = "0000fff2-0000-1000-8000-00805f9b34fb"
RX_CHAR = "0000fff1-0000-1000-8000-00805f9b34fb"

def calc_csum(data):
    return sum(data) & 0xFF

def make_frame(addr, cmd, payload=b""):
    frame = bytes([0x57, 0xab, addr, cmd, len(payload)]) + payload
    return frame + bytes([calc_csum(frame)])

async def main():
    dev = None
    for d in await BleakScanner.discover(timeout=5):
        if d.address == BLE_ADDR:
            dev = d
            break
    if not dev:
        print("Device not found")
        return

    async with BleakClient(dev) as client:
        print("Connected to BLE device")

        # Send CONNECT to 192.168.11.2:22
        print("Sending CONNECT to 192.168.11.2:22...")
        conn_frame = make_frame(0x00, 0x10, bytes([0xc0, 0xa8, 0x0b, 0x02, 0x00, 0x16]))
        await client.write_gatt_char(TX_CHAR, conn_frame)
        await asyncio.sleep(0.5)

        # Wait for CONNECT_RESP
        responses = []
        def handler(sender, data):
            hex_str = " ".join(f"{b:02x}" for b in data)
            responses.append(data)
            print(f"  RX: {hex_str}")
        await client.start_notify(RX_CHAR, handler)

        print("Waiting 3 seconds for CONNECT_RESP and any data push...")
        await asyncio.sleep(3)

        await client.stop_notify(RX_CHAR)

        # Send DISCONNECT
        print("Sending DISCONNECT...")
        await client.write_gatt_char(TX_CHAR, make_frame(0x00, 0x12, bytes([0x00])))
        await asyncio.sleep(0.3)

asyncio.run(main())
