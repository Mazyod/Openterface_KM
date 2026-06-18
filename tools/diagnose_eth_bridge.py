#!/usr/bin/env python3
"""Diagnostic script for ETH bridge testing.

This script provides detailed diagnostics for the BLE-Ethernet bridge,
including LED pattern verification, connection state tracking, and
packet-level analysis.
"""

import sys
import time
import argparse
import asyncio
from pathlib import Path

# Add repo root to path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.ble_eth_tunnel import BleEthClient, TunnelEvent, STATUS_SUCCESS, STATUS_TIMEOUT

# LED diagnostic patterns (from firmware)
LED_DIAG_CONNECT_RX = 90
LED_DIAG_SYN_SENT = 91
LED_DIAG_SYN_ACK_RX = 92
LED_DIAG_TIMEOUT = 93


async def test_ping(client: BleEthClient):
    """Test PING/PONG to verify BLE data path."""
    print("\n=== PING/PONG Test ===")
    print("Testing BLE data path with PING/PONG...")

    test_data = b"HELLO_BLE_ETH"
    try:
        pong = await client.ping(test_data)
        if pong == test_data:
            print(f"✓ PING/PONG successful: sent {len(test_data)} bytes, received {len(pong)} bytes")
            return True
        else:
            print(f"✗ PING/PONG mismatch: expected {test_data!r}, got {pong!r}")
            return False
    except Exception as e:
        print(f"✗ PING/PONG failed: {e}")
        return False


async def test_info(client: BleEthClient):
    """Get device info and capabilities."""
    print("\n=== Device Info ===")

    try:
        info = await client.get_info()
        print(f"Protocol version: {info.get('version', 'unknown')}")
        print(f"Max connections: {info.get('max_connections', 'unknown')}")
        print(f"Max fragment payload: {info.get('max_fragment_payload', 'unknown')} bytes")
        print(f"Features: 0x{info.get('features', 0):02X}")

        features = info.get('features', 0)
        if features & 0x01:
            print("  - TCP tunneling supported")
        if features & 0x02:
            print("  - SSH optimized mode supported")
        if features & 0x04:
            print("  - Metadata ACK supported")
        if features & 0x08:
            print("  - Receiver window feedback supported")

        return True
    except Exception as e:
        print(f"✗ Failed to get device info: {e}")
        return False


async def test_connect(client: BleEthClient, target_ip: str, target_port: int, timeout: float = 15.0):
    """Test CONNECT command and track the flow."""
    print(f"\n=== CONNECT Test to {target_ip}:{target_port} ===")

    print("\nExpected flow:")
    print("  1. BLE device receives CONNECT")
    print("  2. LED pattern 91 (SYN_SENT) - TCP SYN sent to HOST (Mac)")
    print("  3. HOST forwards SYN to TARGET via CDC-ECM")
    print("  4. TARGET responds with SYN-ACK")
    print("  5. LED pattern 92 (SYN_ACK_RX) - SYN-ACK received from TARGET")
    print("  6. Device sends CONNECT_RESP to BLE with STATUS_SUCCESS")
    print("\nIf pattern 91 seen but NOT 92:")
    print("  → HOST (Mac) is not forwarding SYN to TARGET")
    print("  → Check: TARGET PC is connected to firmware USB port")
    print("  → Check: TARGET PC has CDC-ECM driver installed")
    print("  → Check: TARGET PC network interface is up")
    print("\nIf pattern 93 (TIMEOUT) seen:")
    print("  → SYN-ACK not received within 5 seconds")
    print("  → TARGET may not be responding to SYN")

    print(f"\nAttempting CONNECT with {timeout}s timeout...")
    start_time = time.time()

    try:
        conn_id = await client.open_tunnel(target_ip, target_port, timeout=timeout)
        elapsed = time.time() - start_time
        print(f"✓ CONNECT successful! conn_id={conn_id}, elapsed={elapsed:.2f}s")
        return True
    except asyncio.TimeoutError:
        elapsed = time.time() - start_time
        print(f"✗ CONNECT timeout after {elapsed:.2f}s")
        print("\nThis indicates the TCP handshake did not complete.")
        print("Check the LED patterns observed during the test.")
        return False
    except Exception as e:
        elapsed = time.time() - start_time
        error_msg = str(e)
        if "0xE1" in error_msg or "STATUS_TIMEOUT" in error_msg:
            print(f"✗ CONNECT failed with STATUS_TIMEOUT (0xE1) after {elapsed:.2f}s")
            print("\nThis indicates the firmware sent SYN but never received SYN-ACK.")
            print("The target is not responding to TCP SYN packets.")
        else:
            print(f"✗ CONNECT failed: {error_msg}")
        return False


async def diagnostic_session(device_name: str, target_ip: str, target_port: int, timeout: float):
    """Run a complete diagnostic session."""
    print("=" * 70)
    print("ETH Bridge Diagnostic Session")
    print("=" * 70)

    # Create BLE client
    client = BleEthClient(device_name=device_name)

    # Track tunnel events
    tunnel_events = []

    def event_handler(event: TunnelEvent):
        tunnel_events.append(event)
        event_type = {
            1: "CONNECTED",
            2: "DISCONNECTED",
            3: "DATA_RECEIVED",
            4: "ERROR",
        }.get(event.type, f"UNKNOWN({event.type})")
        print(f"  ← Event: {event_type} conn_id={event.conn_id}")
        if event.type == 4 and event.error_msg:  # ERROR
            print(f"    Error: {event.error_msg}")

    client.add_event_callback(event_handler)

    try:
        # Connect to BLE device
        print(f"\nConnecting to BLE device '{device_name}'...")
        await client.connect()
        print("✓ BLE connection established")

        # Get device info
        await test_info(client)

        # Test PING/PONG
        ping_ok = await test_ping(client)

        # Test CONNECT
        connect_ok = await test_connect(client, target_ip, target_port, timeout)

        # Summary
        print("\n" + "=" * 70)
        print("Diagnostic Summary")
        print("=" * 70)
        print(f"BLE connection: ✓")
        print(f"PING/PONG: {'✓' if ping_ok else '✗'}")
        print(f"CONNECT: {'✓' if connect_ok else '✗'}")
        print(f"\nTunnel events received: {len(tunnel_events)}")
        for event in tunnel_events:
            event_type = {
                1: "CONNECTED",
                2: "DISCONNECTED",
                3: "DATA_RECEIVED",
                4: "ERROR",
            }.get(event.type, f"UNKNOWN({event.type})")
            print(f"  - {event_type} conn_id={event.conn_id}")

        print("\n" + "=" * 70)
        print("Next Steps")
        print("=" * 70)
        if not connect_ok:
            print("CONNECT failed. To debug:")
            print("1. Check LED patterns during test (observe PC7 LED on device)")
            print("2. Verify TARGET PC is physically connected via USB")
            print("3. Check TARGET PC network interface:")
            print("   - On TARGET PC, run: ifconfig (or ipconfig on Windows)")
            print("   - Look for CDC-ECM interface with IP 192.168.11.x")
            print("4. Check TARGET PC firewall settings")
            print("5. Verify SSH server is running on TARGET (if port 22)")

    except Exception as e:
        print(f"\n✗ Diagnostic session failed: {e}")
        import traceback
        traceback.print_exc()
    finally:
        # Disconnect
        try:
            await client.disconnect()
            print("\n✓ BLE disconnected")
        except Exception:
            pass

    print("\n" + "=" * 70)


def main():
    parser = argparse.ArgumentParser(description="ETH Bridge Diagnostic Tool")
    parser.add_argument(
        "--device-name",
        default="KVM-GO",
        help="BLE device name (default: KVM-GO)"
    )
    parser.add_argument(
        "--target-ip",
        default="192.168.11.2",
        help="Target IP address (default: 192.168.11.2)"
    )
    parser.add_argument(
        "--target-port",
        type=int,
        default=22,
        help="Target port (default: 22)"
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=15.0,
        help="CONNECT timeout in seconds (default: 15.0)"
    )

    args = parser.parse_args()

    print(f"Configuration:")
    print(f"  Device: {args.device_name}")
    print(f"  Target: {args.target_ip}:{args.target_port}")
    print(f"  Timeout: {args.timeout}s")

    asyncio.run(diagnostic_session(
        args.device_name,
        args.target_ip,
        args.target_port,
        args.timeout
    ))


if __name__ == "__main__":
    main()
