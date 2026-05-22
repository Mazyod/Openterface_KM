#!/usr/bin/env python3
"""Test SSH over BLE via the BLE-Eth tunnel protocol.

This script connects to the Openterface device over BLE, opens a TCP
tunnel to an SSH server, and provides a local port that forwards all
traffic through the BLE tunnel.

Usage:
    # Basic: tunnel to SSH server at 192.168.1.100:22
    python tools/test_ble_eth_ssh.py --target 192.168.1.100

    # Custom SSH port
    python tools/test_ble_eth_ssh.py --target 192.168.1.100 --port 2222

    # By BLE address (if you know it)
    python tools/test_ble_eth_ssh.py --ble-addr AA:BB:CC:DD:EE:FF

    # Verbose logging
    python tools/test_ble_eth_ssh.py --target 192.168.1.100 -v

    # Interactive mode: starts the proxy and waits for you to SSH manually
    python tools/test_ble_eth_ssh.py --target 192.168.1.100 --interactive

    # Quick test: connects, sends a test payload, and exits
    python tools/test_ble_eth_ssh.py --target 192.168.1.100 --test-only

After the proxy starts, open another terminal and run:
    ssh -p 8022 user@localhost

All SSH traffic will be tunneled through BLE to the target machine.
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import sys
import time

# Add parent directory to path for imports
sys.path.insert(0, ".")

from tools.ble_eth_tunnel import (
    BleEthClient,
    BleTcpProxy,
    TunnelEvent,
    MAX_PAYLOAD_LEN,
    build_connect,
    build_disconnect,
    build_info_request,
    build_ping,
    build_data_fragmented,
    build_data_single,
    FrameParser,
    DataReassembler,
    SERVICE_UUID,
    TX_CHAR_UUID,
    RX_CHAR_UUID,
    CMD_INFO_RESP,
    CMD_PONG,
    STATUS_SUCCESS,
)

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Unit tests for frame building and parsing
# ---------------------------------------------------------------------------

def test_frame_build_parse():
    """Test that frames can be built, sent through parser, and decoded."""
    parser = FrameParser()

    # Test CONNECT frame
    frame = build_connect("192.168.1.100", 22)
    print(f"  CONNECT frame ({len(frame)} bytes): {frame.hex(' ')}")
    assert frame[0] == 0x57 and frame[1] == 0xAB
    assert frame[3] == 0x10
    assert frame[4] == 6  # IP(4) + port(2)

    parser.feed(frame)
    assert len(parser.received_frames) == 1
    pf = parser.received_frames.popleft()
    assert pf.addr == 0x00
    assert pf.cmd == 0x10
    assert pf.payload[:4] == bytes([192, 168, 1, 100])
    print("  [PASS] CONNECT frame build/parse")

    # Test INFO request
    frame = build_info_request()
    parser.feed(frame)
    assert len(parser.received_frames) == 1
    pf = parser.received_frames.popleft()
    assert pf.cmd == 0x1F
    assert len(pf.payload) == 0
    print("  [PASS] INFO frame build/parse")

    # Test DISCONNECT frame
    frame = build_disconnect(0)
    parser.feed(frame)
    assert len(parser.received_frames) == 1
    pf = parser.received_frames.popleft()
    assert pf.cmd == 0x12
    assert pf.payload == bytes([0])
    print("  [PASS] DISCONNECT frame build/parse")


def test_checksum():
    """Test checksum calculation."""
    # Manually calculate checksum for a known frame
    head = bytes([0x57, 0xAB, 0x00, 0x10, 0x06])
    payload = bytes([192, 168, 1, 100, 0x00, 0x16])
    expected = (sum(head) + sum(payload)) & 0xFF

    frame = build_connect("192.168.1.100", 22)
    actual = frame[-1]
    assert actual == expected, f"Checksum mismatch: {actual} != {expected}"
    print("  [PASS] Checksum calculation")


def test_fragmentation():
    """Test data fragmentation and reassembly."""
    reassembler = DataReassembler()

    # Test small data (no fragmentation needed)
    data = b"hello"
    frames = build_data_fragmented(0, data)
    assert len(frames) == 1
    # Extract payload: frame[5:5+len] (frame format: head1 head2 addr cmd len payload... csum)
    payload_len = frames[0][4]
    result = reassembler.feed(frames[0][5:5 + payload_len])
    assert result is not None
    assert result.conn_id == 0
    assert result.data == data
    print("  [PASS] Single-fragment data")

    # Test large data (requires fragmentation)
    large_data = bytes(range(256)) * 3  # 768 bytes
    frames = build_data_fragmented(1, large_data)
    assert len(frames) > 1, "Should be fragmented"
    print(f"  Large data ({len(large_data)} bytes) split into {len(frames)} fragments")

    reassembler2 = DataReassembler()
    for i, frame in enumerate(frames):
        payload_len = frame[4]
        payload = frame[5:5 + payload_len]
        result = reassembler2.feed(payload)
        if i < len(frames) - 1:
            assert result is None, f"Should not complete at fragment {i}"
        else:
            assert result is not None, "Should complete at last fragment"
            assert result.conn_id == 1
            assert result.data == large_data

    print("  [PASS] Multi-fragment data reassembly")

    # Test out-of-order detection
    reassembler3 = DataReassembler()
    frames = build_data_fragmented(2, b"test1234" * 40)  # 320 bytes = 2 fragments

    # Feed second fragment first (should abort)
    payload2_len = frames[1][4]
    payload2 = frames[1][5:5 + payload2_len]
    result = reassembler3.feed(payload2)
    assert result is None, "Out of order should return None"

    print("  [PASS] Out-of-order fragment detection")


def test_parser_resync():
    """Test that the parser can resync after garbage data."""
    parser = FrameParser()

    # Feed garbage
    parser.feed(bytes([0xDE, 0xAD, 0xBE, 0xEF]))
    assert len(parser.received_frames) == 0

    # Feed valid frame
    frame = build_info_request()
    parser.feed(frame)
    assert len(parser.received_frames) == 1
    print("  [PASS] Parser resync after garbage")

    # Test partial frame then completion
    parser2 = FrameParser()
    frame = build_disconnect(5)
    parser2.feed(frame[:3])  # partial
    assert len(parser2.received_frames) == 0
    parser2.feed(frame[3:])  # rest
    assert len(parser2.received_frames) == 1
    print("  [PASS] Partial frame then completion")


def run_unit_tests():
    """Run all unit tests."""
    print("=" * 60)
    print("BLE-Eth Protocol Unit Tests")
    print("=" * 60)

    test_frame_build_parse()
    test_checksum()
    test_fragmentation()
    test_parser_resync()

    print("=" * 60)
    print("All unit tests passed!")
    print("=" * 60)


# ---------------------------------------------------------------------------
# BLE connection test
# ---------------------------------------------------------------------------

async def test_ble_connection(args):
    """Test BLE connection and device discovery."""
    print("=" * 60)
    print("BLE Connection Test")
    print("=" * 60)

    client = BleEthClient(
        device_name=args.device_name,
        ble_address=args.ble_addr,
    )

    try:
        print(f"\nScanning for BLE device '{args.device_name}'...")
        await client.connect()
        print(f"  Connected!")

        print("\nFetching device info...")
        info = await client.get_info()
        print(f"  Protocol version: {info['version']}")
        print(f"  Max connections:  {info['max_connections']}")
        print(f"  Max frag payload: {info['max_fragment_payload']} bytes")
        print(f"  Features:         0x{info['features']:02X}")
        print(f"  TCP tunneling:    {'Yes' if info['features'] & 0x01 else 'No'}")
        print(f"  SSH optimized:    {'Yes' if info['features'] & 0x02 else 'No'}")

        return 0

    except Exception as e:
        print(f"\n  ERROR: {e}")
        return 1
    finally:
        await client.disconnect()


# ---------------------------------------------------------------------------
# BLE-Eth data path test (PING/PONG, no network needed)
# ---------------------------------------------------------------------------

async def test_ble_eth_data_path(args):
    """Test BLE-Eth round-trip data path using PING/PONG."""
    print("=" * 60)
    print("BLE-Eth Data Path Test (PING/PONG)")
    print("=" * 60)

    client = BleEthClient(
        device_name=args.device_name,
        ble_address=args.ble_addr,
    )

    try:
        # Step 1: Connect to BLE device
        print("\n[1/3] Connecting to BLE device...")
        await client.connect()
        print("  BLE connected!")

        # Step 2: Get device info
        print("\n[2/3] Getting device capabilities...")
        info = await client.get_info()
        print(f"  Version: {info['version']}, Features: 0x{info['features']:02X}")

        # Step 3: PING test
        print("\n[3/3] Testing BLE-Eth round-trip (PING/PONG)...")

        # Test 1: Simple ping
        print("  Simple ping (5 bytes)...")
        pong = await client.ping(b"hello")
        assert pong == b"hello", f"PONG mismatch: {pong!r} != b'hello'"
        print("  [PASS] Simple ping")

        # Test 2: Larger payload
        print("  Large payload ping (247 bytes)...")
        large_data = bytes(range(247))
        pong = await client.ping(large_data)
        assert pong == large_data, f"PONG mismatch for large payload"
        print("  [PASS] Large payload ping")

        # Test 3: Max payload ping (frame must fit in uint8_t notify length)
        print(f"  Max payload ping ({MAX_PAYLOAD_LEN} bytes)...")
        max_data = bytes([0xAA] * MAX_PAYLOAD_LEN)
        pong = await client.ping(max_data)
        assert pong == max_data, f"PONG mismatch for max payload"
        print("  [PASS] Max payload ping")

        print("\n" + "=" * 60)
        print("BLE-Eth Data Path Test PASSED!")
        print("=" * 60)
        return 0

    except Exception as e:
        print(f"\n  ERROR: {e}")
        import traceback
        traceback.print_exc()
        return 1
    finally:
        await client.disconnect()
        print("Disconnected from BLE device.")


# ---------------------------------------------------------------------------
# Full SSH tunnel test
# ---------------------------------------------------------------------------

async def test_ssh_tunnel(args):
    """Run the full BLE SSH tunnel test."""
    print("=" * 60)
    print("BLE SSH Tunnel Test")
    print("=" * 60)
    print(f"\nTarget SSH server: {args.target}:{args.port}")
    print(f"Local proxy port:  {args.local_port}")
    print(f"BLE device:        {args.device_name}")
    print()

    client = BleEthClient(
        device_name=args.device_name,
        ble_address=args.ble_addr,
    )

    def event_handler(evt: TunnelEvent):
        type_names = {
            TunnelEvent.Type.CONNECTED: "CONNECTED",
            TunnelEvent.Type.DISCONNECTED: "DISCONNECTED",
            TunnelEvent.Type.DATA_RECEIVED: "DATA",
            TunnelEvent.Type.ERROR: "ERROR",
        }
        if evt.type == TunnelEvent.Type.DATA_RECEIVED:
            logger.info(f"  [EVENT] {type_names[evt.type]} conn={evt.conn_id} ({len(evt.data)} bytes)")
        else:
            logger.info(f"  [EVENT] {type_names[evt.type]} conn={evt.conn_id} {evt.error_msg}")

    client.add_event_callback(event_handler)

    try:
        # Step 1: Connect to BLE device
        print("[1/4] Connecting to BLE device...")
        await client.connect()
        print("  BLE connected!")

        # Step 2: Get device info
        print("\n[2/4] Getting device capabilities...")
        info = await client.get_info()
        print(f"  Version: {info['version']}, Features: 0x{info['features']:02X}")

        # Step 3: Open tunnel to SSH server
        print(f"\n[3/4] Opening tunnel to {args.target}:{args.port}...")
        conn_id = await client.open_tunnel(args.target, args.port)
        print(f"  Tunnel opened! conn_id={conn_id}")

        # Step 4: Start local proxy
        print(f"\n[4/4] Starting local TCP proxy on 127.0.0.1:{args.local_port}...")

        proxy = BleTcpProxy(
            client=client,
            local_port=args.local_port,
            target_ip=args.target,
            target_port=args.port,
        )
        await proxy.start()

        print("\n" + "=" * 60)
        print("BLE SSH Tunnel Active!")
        print("=" * 60)
        print(f"\n  Local endpoint:  127.0.0.1:{args.local_port}")
        print(f"  BLE tunnel  ->   {args.target}:{args.port}")
        print(f"  Connection ID:   {conn_id}")
        print()
        print("  To connect via SSH:")
        print(f"    ssh -p {args.local_port} <user>@127.0.0.1")
        print()

        if args.test_only:
            # Quick connectivity test
            print("[TEST] Attempting TCP connect through tunnel...")
            try:
                reader, writer = await asyncio.wait_for(
                    asyncio.open_connection("127.0.0.1", args.local_port),
                    timeout=10,
                )
                # Read SSH banner
                banner = await asyncio.wait_for(reader.readline(), timeout=10)
                print(f"  SSH Banner: {banner.decode().strip()}")
                writer.close()
                await writer.wait_closed()
                print("  [PASS] TCP tunnel works!")
            except Exception as e:
                print(f"  [FAIL] TCP test failed: {e}")

            await proxy.stop()
            await client.close_tunnel(conn_id)
            return

        # Interactive mode: keep proxy running
        print("\nProxy is running. Press Ctrl+C to stop.")
        print()

        try:
            while True:
                await asyncio.sleep(1)
        except asyncio.CancelledError:
            pass
        except KeyboardInterrupt:
            pass

        print("\nStopping proxy...")
        await proxy.stop()
        await client.close_tunnel(conn_id)

    except Exception as e:
        print(f"\n  ERROR: {e}")
        import traceback
        traceback.print_exc()
    finally:
        await client.disconnect()
        print("Disconnected from BLE device.")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Test SSH over BLE via BLE-Eth tunnel protocol.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )

    # Target SSH server
    parser.add_argument(
        "--target", type=str, default="192.168.1.100",
        help="IP address of the SSH server to tunnel to (default: 192.168.1.100)",
    )
    parser.add_argument(
        "--port", type=int, default=22,
        help="SSH server port (default: 22)",
    )

    # BLE device
    parser.add_argument(
        "--device-name", type=str, default="KVM-GO",
        help="BLE device name to scan for (default: KVM-GO)",
    )
    parser.add_argument(
        "--ble-addr", type=str, default=None,
        help="BLE device address (optional, skips scan if known)",
    )

    # Local proxy
    parser.add_argument(
        "--local-port", type=int, default=8022,
        help="Local TCP port for the proxy (default: 8022)",
    )

    # Test modes
    parser.add_argument(
        "--test-only", action="store_true",
        help="Quick test: connect, verify tunnel to real SSH server, then exit",
    )
    parser.add_argument(
        "--data-only", action="store_true",
        help="Test BLE-Eth round-trip data path using PING/PONG (no network needed)",
    )
    parser.add_argument(
        "--unit-tests", action="store_true",
        help="Run protocol unit tests only (no BLE)",
    )
    parser.add_argument(
        "--ble-only", action="store_true",
        help="Test BLE connection and device info only",
    )
    parser.add_argument(
        "--interactive", action="store_true",
        help="Start proxy and wait for manual SSH connection",
    )

    parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="Enable verbose logging",
    )

    args = parser.parse_args()

    # Logging
    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(
        level=log_level,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )

    # Unit tests mode
    if args.unit_tests:
        run_unit_tests()
        return 0

    # Check bleak is installed
    try:
        import bleak
    except ImportError:
        print("ERROR: bleak library not installed.")
        print("Install with: pip install bleak")
        return 1

    # Data path test (PING/PONG, no network needed)
    if args.data_only:
        return asyncio.run(test_ble_eth_data_path(args))

    # BLE-only test mode
    if args.ble_only:
        return asyncio.run(test_ble_connection(args))

    # Full SSH tunnel test
    return asyncio.run(test_ssh_tunnel(args))


if __name__ == "__main__":
    raise SystemExit(main())
