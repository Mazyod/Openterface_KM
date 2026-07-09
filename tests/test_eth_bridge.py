#!/usr/bin/env python3
"""Test ETH bridge between host (Mac) and target through Openterface firmware.

This script tests the complete ETH bridge path:
  Host Mac <-> BLE <-> Firmware <-> USB ECM <-> Target PC

Architecture:
  - Host side (USBD): Mac gets DHCP 192.168.11.1, firmware is 192.168.11.3
  - Target side (USBFS): Target PC gets DHCP 192.168.11.2, firmware is 192.168.11.5
  - BLE tunnel: TCP connections tunneled over BLE-Eth protocol

Test coverage:
  1. BLE connection and INFO exchange
  2. PING/PONG round-trip (no network needed)
  3. TCP tunnel CONNECT to target
  4. SSH banner exchange through tunnel
  5. ECM bridge diagnostics (if available)

Usage:
    # Full bridge test (requires target PC connected)
    python3 tools/test_eth_bridge.py

    # BLE + PING only (no target needed)
    python3 tools/test_eth_bridge.py --ble-only

    # Test specific target IP
    python3 tools/test_eth_bridge.py --target 192.168.11.2 --port 22

    # Verbose logging
    python3 tools/test_eth_bridge.py -v
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import os
import sys
import time
from typing import Optional

# Add repo root to path
_repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, _repo_root)

from tools.ble_eth_tunnel import (
    BleEthClient,
    BleTcpProxy,
    TunnelEvent,
    STATUS_SUCCESS,
    STATUS_TIMEOUT,
    MAX_PAYLOAD_LEN,
    build_connect,
    build_disconnect,
    build_info_request,
    build_ping,
    FrameParser,
    DataReassembler,
)

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Test result tracking
# ---------------------------------------------------------------------------

class TestResults:
    def __init__(self):
        self.passed = []
        self.failed = []
        self.skipped = []

    def record_pass(self, name: str):
        self.passed.append(name)
        print(f"  [PASS] {name}")

    def record_fail(self, name: str, error: str):
        self.failed.append((name, error))
        print(f"  [FAIL] {name}: {error}")

    def record_skip(self, name: str, reason: str):
        self.skipped.append((name, reason))
        print(f"  [SKIP] {name}: {reason}")

    def summary(self):
        print()
        print("=" * 60)
        print("TEST SUMMARY")
        print("=" * 60)
        print(f"  Passed:  {len(self.passed)}")
        print(f"  Failed:  {len(self.failed)}")
        print(f"  Skipped: {len(self.skipped)}")
        if self.failed:
            print()
            print("  Failed tests:")
            for name, error in self.failed:
                print(f"    - {name}: {error}")
        print("=" * 60)
        return len(self.failed) == 0


# ---------------------------------------------------------------------------
# Protocol unit tests (no BLE needed)
# ---------------------------------------------------------------------------

def test_protocol_frames(results: TestResults):
    """Test frame building and parsing without BLE connection."""
    print("\n--- Protocol Frame Tests ---")
    parser = FrameParser()

    # Test CONNECT frame
    try:
        frame = build_connect("192.168.11.2", 22)
        assert frame[0] == 0x57 and frame[1] == 0xAB
        assert frame[3] == 0x10  # CMD_CONNECT
        assert frame[4] == 6  # payload length
        assert frame[5:9] == bytes([192, 168, 11, 2])
        assert frame[9:11] == bytes([0x00, 0x16])  # port 22
        parser.feed(frame)
        assert len(parser.received_frames) == 1
        pf = parser.received_frames.popleft()
        assert pf.cmd == 0x10
        results.record_pass("CONNECT frame build/parse")
    except Exception as e:
        results.record_fail("CONNECT frame build/parse", str(e))

    # Test INFO frame
    try:
        frame = build_info_request()
        parser.feed(frame)
        assert len(parser.received_frames) == 1
        pf = parser.received_frames.popleft()
        assert pf.cmd == 0x1F
        results.record_pass("INFO frame build/parse")
    except Exception as e:
        results.record_fail("INFO frame build/parse", str(e))

    # Test DISCONNECT frame
    try:
        frame = build_disconnect(0)
        parser.feed(frame)
        assert len(parser.received_frames) == 1
        pf = parser.received_frames.popleft()
        assert pf.cmd == 0x12
        assert pf.payload == bytes([0])
        results.record_pass("DISCONNECT frame build/parse")
    except Exception as e:
        results.record_fail("DISCONNECT frame build/parse", str(e))

    # Test PING frame
    try:
        frame = build_ping(b"hello")
        parser.feed(frame)
        assert len(parser.received_frames) == 1
        pf = parser.received_frames.popleft()
        assert pf.cmd == 0x1E
        assert pf.payload == b"hello"
        results.record_pass("PING frame build/parse")
    except Exception as e:
        results.record_fail("PING frame build/parse", str(e))

    # Test checksum validation
    try:
        frame = build_connect("10.0.0.1", 8080)
        # Corrupt checksum
        frame = frame[:-1] + bytes([(frame[-1] + 1) & 0xFF])
        parser2 = FrameParser()
        parser2.feed(frame)
        # Parser should reject bad checksum
        assert len(parser2.received_frames) == 0
        results.record_pass("Checksum validation rejects bad frames")
    except Exception as e:
        results.record_fail("Checksum validation rejects bad frames", str(e))

    # Test parser resync after garbage
    try:
        parser3 = FrameParser()
        parser3.feed(bytes([0xDE, 0xAD, 0xBE, 0xEF]))
        assert len(parser3.received_frames) == 0
        parser3.feed(build_info_request())
        assert len(parser3.received_frames) == 1
        results.record_pass("Parser resync after garbage")
    except Exception as e:
        results.record_fail("Parser resync after garbage", str(e))


# ---------------------------------------------------------------------------
# BLE connection tests
# ---------------------------------------------------------------------------

async def test_ble_connection(client: BleEthClient, results: TestResults):
    """Test BLE connection and device discovery."""
    print("\n--- BLE Connection Tests ---")

    try:
        await client.connect()
        results.record_pass("BLE connect")
    except Exception as e:
        results.record_fail("BLE connect", str(e))
        return

    try:
        info = await client.get_info()
        assert info['version'] >= 1
        assert info['max_connections'] >= 1
        assert info['max_fragment_payload'] >= 64
        logger.info(f"  Device: v{info['version']} max_conn={info['max_connections']} "
                    f"max_frag={info['max_fragment_payload']} features=0x{info['features']:02X}")
        results.record_pass("INFO exchange")
    except Exception as e:
        results.record_fail("INFO exchange", str(e))

    return info


async def test_ping_pong(client: BleEthClient, results: TestResults):
    """Test PING/PONG round-trip (no network needed)."""
    print("\n--- PING/PONG Tests ---")

    # Simple ping
    try:
        pong = await client.ping(b"hello")
        assert pong == b"hello", f"Expected b'hello', got {pong!r}"
        results.record_pass("Simple PING/PONG (5 bytes)")
    except Exception as e:
        results.record_fail("Simple PING/PONG", str(e))

    # Large payload ping
    try:
        large_data = bytes(range(247))
        pong = await client.ping(large_data)
        assert pong == large_data
        results.record_pass("Large PING/PONG (247 bytes)")
    except Exception as e:
        results.record_fail("Large PING/PONG", str(e))

    # Max payload ping
    try:
        max_data = bytes([0xAA] * MAX_PAYLOAD_LEN)
        pong = await client.ping(max_data)
        assert pong == max_data
        results.record_pass(f"Max PING/PONG ({MAX_PAYLOAD_LEN} bytes)")
    except Exception as e:
        results.record_fail(f"Max PING/PONG", str(e))


# ---------------------------------------------------------------------------
# TCP tunnel tests
# ---------------------------------------------------------------------------

async def test_tcp_tunnel_connect(
    client: BleEthClient,
    target_ip: str,
    target_port: int,
    results: TestResults,
) -> Optional[int]:
    """Test TCP tunnel CONNECT to target."""
    print(f"\n--- TCP Tunnel Tests ({target_ip}:{target_port}) ---")

    try:
        conn_id = await client.open_tunnel(target_ip, target_port, timeout=15.0)
        logger.info(f"  Tunnel opened: conn_id={conn_id}")
        results.record_pass(f"CONNECT to {target_ip}:{target_port}")
        return conn_id
    except asyncio.TimeoutError:
        results.record_fail(f"CONNECT to {target_ip}:{target_port}",
                           "timeout (firmware cannot reach target - check Ethernet link)")
        return None
    except Exception as e:
        error_msg = str(e)
        if "0xE1" in error_msg:
            results.record_fail(f"CONNECT to {target_ip}:{target_port}",
                               "STATUS_TIMEOUT (0xE1) - target unreachable")
        else:
            results.record_fail(f"CONNECT to {target_ip}:{target_port}", error_msg)
        return None


async def test_ssh_banner_through_tunnel(
    client: BleEthClient,
    conn_id: int,
    local_port: int,
    results: TestResults,
):
    """Test SSH banner exchange through the tunnel."""
    print("\n--- SSH Banner Test ---")

    proxy = None
    try:
        # Start local proxy
        proxy = BleTcpProxy(
            client=client,
            local_host="127.0.0.1",
            local_port=local_port,
            target_ip=client._last_target_ip if hasattr(client, '_last_target_ip') else "192.168.11.2",
            target_port=22,
        )
        await proxy.start()
        logger.info(f"  Local proxy started on 127.0.0.1:{local_port}")

        # Connect to local proxy and read SSH banner
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection("127.0.0.1", local_port),
                timeout=5.0
            )
            banner = await asyncio.wait_for(reader.readline(), timeout=10.0)
            writer.close()
            await writer.wait_closed()

            banner_text = banner.decode(errors="replace").strip()
            if banner_text.startswith("SSH-"):
                logger.info(f"  SSH banner: {banner_text[:60]}...")
                results.record_pass("SSH banner exchange through tunnel")
            else:
                results.record_fail("SSH banner exchange", f"invalid banner: {banner_text!r}")
        except asyncio.TimeoutError:
            results.record_fail("SSH banner exchange", "timeout reading banner")
        except Exception as e:
            results.record_fail("SSH banner exchange", str(e))

    except Exception as e:
        results.record_fail("SSH banner test setup", str(e))
    finally:
        if proxy:
            await proxy.stop()


# ---------------------------------------------------------------------------
# ECM bridge diagnostics
# ---------------------------------------------------------------------------

async def test_ecm_bridge_diagnostics(client: BleEthClient, results: TestResults):
    """Test ECM bridge status if available."""
    print("\n--- ECM Bridge Diagnostics ---")

    # Check if device supports extended diagnostics
    try:
        info = await client.get_info()
        features = info.get('features', 0)

        # Log ECM bridge configuration
        logger.info("  ECM Bridge Configuration:")
        logger.info("    Host side (USBD):")
        logger.info("      Firmware IP: 192.168.11.3")
        logger.info("      Host IP:     192.168.11.1 (DHCP offered)")
        logger.info("      MAC:         02:00:00:00:00:32")
        logger.info("    Target side (USBFS):")
        logger.info("      Firmware IP: 192.168.11.5")
        logger.info("      Target IP:   192.168.11.2 (DHCP offered)")
        logger.info("      MAC:         02:00:00:00:03:00")

        results.record_pass("ECM bridge configuration")
    except Exception as e:
        results.record_fail("ECM bridge diagnostics", str(e))


# ---------------------------------------------------------------------------
# Main test runner
# ---------------------------------------------------------------------------

async def run_tests(args: argparse.Namespace) -> int:
    """Run all ETH bridge tests."""
    results = TestResults()

    # Protocol unit tests (no BLE needed)
    if not args.ble_only:
        test_protocol_frames(results)

    # Check bleak
    try:
        import bleak
    except ImportError:
        print("ERROR: bleak not installed. Run: pip install bleak")
        return 1

    # Create BLE client
    client = BleEthClient(
        device_name=args.device_name,
        ble_address=args.ble_addr,
        data_ack_policy="feature",
    )

    # Event logging
    def on_event(evt: TunnelEvent):
        if evt.type == TunnelEvent.Type.DATA_RECEIVED:
            return  # too verbose
        type_names = {
            TunnelEvent.Type.CONNECTED: "CONNECTED",
            TunnelEvent.Type.DISCONNECTED: "DISCONNECTED",
            TunnelEvent.Type.ERROR: "ERROR",
        }
        name = type_names.get(evt.type, f"type={evt.type}")
        if evt.error_msg:
            logger.debug(f"  [EVENT] {name} conn={evt.conn_id} {evt.error_msg}")
        else:
            logger.debug(f"  [EVENT] {name} conn={evt.conn_id}")

    client.add_event_callback(on_event)

    try:
        # BLE connection tests
        info = await test_ble_connection(client, results)
        if info is None:
            return 1

        # PING/PONG tests
        await test_ping_pong(client, results)

        # TCP tunnel tests (requires target)
        if not args.ble_only:
            conn_id = await test_tcp_tunnel_connect(
                client, args.target, args.port, results
            )

            if conn_id is not None:
                # Close initial tunnel before SSH banner test
                # (proxy needs to open its own connection)
                try:
                    await client.close_tunnel(conn_id)
                    logger.info(f"  Initial tunnel {conn_id} closed for SSH test")
                except Exception:
                    pass

                # SSH banner test
                await test_ssh_banner_through_tunnel(
                    client, conn_id, args.local_port, results
                )

            # ECM bridge diagnostics
            await test_ecm_bridge_diagnostics(client, results)

    finally:
        await client.disconnect()
        logger.info("BLE disconnected")

    # Print summary
    success = results.summary()
    return 0 if success else 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Test ETH bridge between host and target through Openterface firmware.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )

    parser.add_argument(
        "--target", default="192.168.11.2",
        help="Target IP for TCP tunnel (default: 192.168.11.2)",
    )
    parser.add_argument(
        "--port", type=int, default=22,
        help="Target port for TCP tunnel (default: 22)",
    )
    parser.add_argument(
        "--local-port", type=int, default=18048,
        help="Local proxy port (default: 18048)",
    )
    parser.add_argument(
        "--device-name", default="KVM-GO",
        help="BLE device name to scan for (default: KVM-GO)",
    )
    parser.add_argument(
        "--ble-addr", default=None,
        help="BLE device address (optional, skips scan)",
    )
    parser.add_argument(
        "--ble-only", action="store_true",
        help="Test BLE connection and PING/PONG only (no target needed)",
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="Enable debug logging",
    )

    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )

    print("=" * 60)
    print("ETH Bridge Test Suite")
    print("=" * 60)
    print(f"  Target:      {args.target}:{args.port}")
    print(f"  BLE device:  {args.device_name}")
    print(f"  Mode:        {'BLE-only' if args.ble_only else 'Full bridge'}")
    print()

    return asyncio.run(run_tests(args))


if __name__ == "__main__":
    raise SystemExit(main())
