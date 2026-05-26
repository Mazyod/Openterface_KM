"""Diagnostic script: connect to device, send CONNECT, watch for BLE responses."""
import asyncio
import time
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

    responses = []

    def handler(sender, data):
        hex_str = " ".join(f"{b:02x}" for b in data)
        responses.append(data)
        print(f"  RX BLE ({len(data)} bytes): {hex_str}")

    async with BleakClient(dev) as client:
        print("Connected to BLE device")
        await client.start_notify(RX_CHAR, handler)
        await asyncio.sleep(0.5)

        # 1. Send INFO request
        print("\n[1] Sending INFO request...")
        info_frame = make_frame(0x00, 0x1F)
        print(f"  TX: {' '.join(f'{b:02x}' for b in info_frame)}")
        await client.write_gatt_char(TX_CHAR, info_frame)
        await asyncio.sleep(1)

        # 2. Send CONNECT to 192.168.11.2:22
        print("\n[2] Sending CONNECT to 192.168.11.2:22...")
        conn_frame = make_frame(0x00, 0x10, bytes([0xc0, 0xa8, 0x0b, 0x02, 0x00, 0x16]))
        print(f"  TX: {' '.join(f'{b:02x}' for b in conn_frame)}")
        await client.write_gatt_char(TX_CHAR, conn_frame)
        await asyncio.sleep(1)

        responses.clear()

        # 3. Send DATA frame (SSH client greeting simulation)
        print("\n[3] Sending DATA frame with SSH greeting...")
        # First, build the tunneled SSH banner
        ssh_data = b"SSH-2.0-test-client\r\n"
        # Fragment header: FIRST, no MORE, count=1, seq=0, conn_id=0
        frag_header = bytes([0x41, 0x00, 0x00])  # FIRST + count=1, seq=0, conn=0
        payload = frag_header + ssh_data
        data_frame = make_frame(0x00, 0x11, payload)
        print(f"  TX ({len(data_frame)} bytes): {' '.join(f'{b:02x}' for b in data_frame)}")
        await client.write_gatt_char(TX_CHAR, data_frame)

        # 4. Wait for responses
        print("\n[4] Waiting for responses (5 seconds)...")
        await asyncio.sleep(5)

        if responses:
            print(f"\nReceived {len(responses)} BLE notifications")
            # Try to parse any DATA_PUSH responses
            for resp in responses:
                if len(resp) >= 5:
                    cmd = resp[3]
                    if cmd == 0x91:  # DATA_PUSH
                        print(f"  -> DATA_PUSH received! payload_len={resp[4]}")
                        if len(resp) > 8:
                            data_payload = resp[8:-1]
                            print(f"  -> Data: {data_payload}")
                    elif cmd == 0xD2:
                        print(f"  -> CONN_CLOSED received")
                    else:
                        print(f"  -> cmd=0x{cmd:02x}, len={resp[4]}")
        else:
            print("\nNo BLE responses received - data path is broken!")

        # 5. Send DISCONNECT
        print("\n[5] Disconnecting...")
        disc_frame = make_frame(0x00, 0x12, bytes([0x00]))
        await client.write_gatt_char(TX_CHAR, disc_frame)
        await asyncio.sleep(0.5)

        await client.stop_notify(RX_CHAR)

asyncio.run(main())
