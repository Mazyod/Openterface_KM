"""Diagnostic: try different ports on the Mac to see what's reachable."""
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

async def test_port(client, port, label, timeout=3):
    """Try connecting to a port and return what responses we get."""
    responses = []
    def handler(sender, data):
        responses.append(data)

    # CONNECT
    await client.write_gatt_char(TX_CHAR,
        make_frame(0x00, 0x10, bytes([0xc0, 0xa8, 0x0b, 0x02, (port >> 8) & 0xFF, port & 0xFF])))
    await asyncio.sleep(0.5)

    await client.start_notify(RX_CHAR, handler)

    # Send data: for SSH, send banner. For others, just empty data to trigger response
    if port == 22:
        ssh_data = b"SSH-2.0-test-client\r\n"
        frag = bytes([0x41, 0x00, 0x00]) + ssh_data
    else:
        frag = bytes([0x41, 0x00, 0x00]) + b"\x00" * 4  # small data

    await client.write_gatt_char(TX_CHAR, make_frame(0x00, 0x11, frag))
    await asyncio.sleep(timeout)

    await client.stop_notify(RX_CHAR)

    # DISCONNECT
    await client.write_gatt_char(TX_CHAR, make_frame(0x00, 0x12, bytes([0x00])))
    await asyncio.sleep(0.3)

    return responses

async def main():
    dev = None
    for d in await BleakScanner.discover(timeout=5):
        if d.address == BLE_ADDR:
            dev = d
            break
    if not dev:
        print("Device not found")
        return

    ports = [
        (22, "SSH"),
        (443, "HTTPS"),
        (80, "HTTP"),
        (88, "Bonjour"),
        (5353, "mDNS"),
    ]

    async with BleakClient(dev) as client:
        print("Connected to BLE device")

        # INFO first
        await client.write_gatt_char(TX_CHAR, make_frame(0x00, 0x1F))
        await asyncio.sleep(0.5)

        for port, label in ports:
            print(f"\nTesting port {port} ({label})...")
            responses = await test_port(client, port, label, timeout=3)
            if responses:
                for r in responses:
                    hex_str = " ".join(f"{b:02x}" for b in r)
                    cmd = r[3] if len(r) > 3 else 0
                    print(f"  RX: {hex_str}  (cmd=0x{cmd:02x})")
                    # Check for DATA_PUSH with actual data
                    if cmd == 0x91 and len(r) > 8:
                        payload = r[8:-1]  # skip header and checksum
                        print(f"  -> DATA payload ({len(payload)} bytes): {payload[:50]}")
            else:
                print(f"  No responses - port closed/filtered or Mac not responding")

asyncio.run(main())
