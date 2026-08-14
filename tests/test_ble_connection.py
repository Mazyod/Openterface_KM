#!/usr/bin/env python3
"""BLE connection integration tests for KeyMod firmware.

Connects to the KeyMod device over BLE and exercises the CH9329 protocol
through the rawpass BLE service (UUID 0xFFF0).

BLE Service layout:
  Service UUID:  0000fff0-0000-1000-8000-00805f9b34fb
  TX (Notify):   0000fff1-0000-1000-8000-00805f9b34fb  (device → host)
  RX (Write):    0000fff2-0000-1000-8000-00805f9b34fb  (host → device)

The CH9329 frame format is the same as the serial path:
  57 AB <addr> <cmd> <len> <payload...> <checksum>

Requirements:
  - bleak (pip install bleak)
  - KeyMod device with BLE enabled and advertising

Usage:
  pytest tests/test_ble_connection.py -v
  OPENTERFACE_BLE_ADDR=XX:XX:XX:XX:XX:XX pytest tests/test_ble_connection.py -v
"""

import asyncio
import os
import struct
import sys
import time

import pytest

# ---------------------------------------------------------------------------
# Skip if bleak is not available
# ---------------------------------------------------------------------------
try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    pytest.skip("bleak not installed — pip install bleak", allow_module_level=True)

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb"
TX_CHAR_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"  # Notify (device → host)
RX_CHAR_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"  # Write  (host → device)

KNOWN_BLE_NAMES = ("KeyMod", "KVM-GO", "Mini-KVM")

# CH9329 protocol constants
CH9329_HEAD1 = 0x57
CH9329_HEAD2 = 0xAB
CMD_GET_INFO      = 0x01
CMD_KB_GENERAL    = 0x02
CMD_KB_MEDIA      = 0x03
CMD_MS_ABS        = 0x04
CMD_MS_REL        = 0x05
CMD_GET_PARA_CFG  = 0x08
CMD_USB_WORK_MODE = 0x30

RESP_OFFSET = 0x80

# Timeouts
CONNECT_TIMEOUT = 15.0
RESPONSE_TIMEOUT = 5.0
SCAN_TIMEOUT = 15.0


# ---------------------------------------------------------------------------
# CH9329 frame helpers
# ---------------------------------------------------------------------------
def ch9329_checksum(data: bytes) -> int:
    return sum(data) & 0xFF


def build_ch9329(addr: int, cmd: int, payload: bytes = b"") -> bytes:
    head = bytes([CH9329_HEAD1, CH9329_HEAD2, addr & 0xFF, cmd & 0xFF, len(payload)])
    frame = head + payload
    return frame + bytes([ch9329_checksum(frame)])


def parse_ch9329(data: bytes):
    """Parse a CH9329 frame, return dict or None."""
    if len(data) < 6:
        return None
    if data[0] != CH9329_HEAD1 or data[1] != CH9329_HEAD2:
        return None
    payload_len = data[4]
    if len(data) < 6 + payload_len:
        return None
    expected = ch9329_checksum(data[:5 + payload_len])
    if data[5 + payload_len] != expected:
        return None
    return {
        "addr": data[2],
        "cmd": data[3],
        "payload": data[5:5 + payload_len],
    }


# ---------------------------------------------------------------------------
# BLE device discovery
# ---------------------------------------------------------------------------
async def _scan_for_device(timeout: float, name_override: str = None, addr_override: str = None):
    """Scan for a known Openterface BLE device."""
    if addr_override:
        return addr_override, name_override or "Unknown"

    devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for addr, (dev, adv) in devices.items():
        name = adv.local_name or dev.name or ""
        if name_override and name == name_override:
            return addr, name
        if not name_override and name in KNOWN_BLE_NAMES:
            return addr, name
    return None, None


def _detect_ble_device():
    """Synchronous wrapper for BLE device scan."""
    name_override = os.environ.get("OPENTERFACE_BLE_NAME")
    addr_override = os.environ.get("OPENTERFACE_BLE_ADDR")

    async def _run():
        return await _scan_for_device(SCAN_TIMEOUT, name_override, addr_override)

    try:
        return asyncio.run(_run())
    except Exception:
        return None, None


# ---------------------------------------------------------------------------
# BLE client fixture
# ---------------------------------------------------------------------------
class BleTestClient:
    """Async BLE test client that collects notifications."""

    def __init__(self, client: BleakClient, loop):
        self.client = client
        self.loop = loop
        self.notifications = bytearray()
        self._notify_event = asyncio.Event()

    def _on_notify(self, sender, data: bytearray):
        self.notifications.extend(data)
        self._notify_event.set()

    async def _enable_notifications(self):
        await self.client.start_notify(TX_CHAR_UUID, self._on_notify)
        await asyncio.sleep(0.3)

    async def _disable_notifications(self):
        await self.client.stop_notify(TX_CHAR_UUID)

    async def _send_and_collect_async(self, frame: bytes, timeout: float = RESPONSE_TIMEOUT) -> bytes:
        """Send a frame via BLE write and collect response notifications."""
        self.notifications.clear()
        self._notify_event.clear()
        await self.client.write_gatt_char(RX_CHAR_UUID, frame, response=False)
        try:
            await asyncio.wait_for(self._wait_complete(), timeout=timeout)
        except asyncio.TimeoutError:
            pass
        await asyncio.sleep(0.1)
        return bytes(self.notifications)

    def send_and_collect(self, frame: bytes, timeout: float = RESPONSE_TIMEOUT) -> bytes:
        """Synchronous wrapper for send_and_collect."""
        return self.loop.run_until_complete(self._send_and_collect_async(frame, timeout))

    async def _wait_complete(self):
        """Wait until no new notifications arrive for 200ms."""
        last_len = 0
        while True:
            await self._notify_event.wait()
            self._notify_event.clear()
            await asyncio.sleep(0.2)
            if len(self.notifications) == last_len:
                break
            last_len = len(self.notifications)


@pytest.fixture(scope="module")
def ble_device():
    """Discover a BLE device or skip."""
    addr, name = _detect_ble_device()
    if addr is None:
        pytest.skip("No KeyMod BLE device found — make sure device is powered on and advertising")
    return addr, name


@pytest.fixture(scope="module")
def ble_client(ble_device):
    """Connect to the BLE device and provide a test client.

    Skips all tests if the BLE connection cannot be established.
    """
    addr, name = ble_device

    async def _connect():
        client = BleakClient(addr, timeout=CONNECT_TIMEOUT)
        try:
            await client.connect()
        except Exception as exc:
            await client.disconnect()
            raise RuntimeError(f"BLE connect to {name} ({addr}) failed: {exc}") from exc
        tc = BleTestClient(client, loop)
        await tc._enable_notifications()
        return tc

    loop = asyncio.new_event_loop()
    try:
        tc = loop.run_until_complete(_connect())
    except RuntimeError as exc:
        loop.close()
        pytest.skip(str(exc))
        return  # unreachable, but makes intent clear

    yield tc

    try:
        loop.run_until_complete(tc.client.disconnect())
    except Exception:
        pass
    loop.close()


# ---------------------------------------------------------------------------
# BLE connection integration tests
# ---------------------------------------------------------------------------
class TestBLEConnection:
    """Live BLE connection tests — require a KeyMod device with BLE enabled."""

    def test_ble_connected(self, ble_client):
        """Verify BLE connection is active."""
        assert ble_client.client.is_connected

    def test_get_info(self, ble_client):
        """GET_INFO (0x01) over BLE — returns device status."""
        frame = build_ch9329(0x00, CMD_GET_INFO)
        resp_data = ble_client.send_and_collect(frame)
        parsed = parse_ch9329(resp_data)
        assert parsed is not None, f"No valid CH9329 response, got {resp_data.hex()}"
        assert parsed["cmd"] == (CMD_GET_INFO | RESP_OFFSET)
        assert len(parsed["payload"]) == 8
        # payload[0] = hardware selector (3 = KeyMod)
        assert parsed["payload"][0] in (1, 2, 3), "Unknown hardware selector"

    def test_keyboard_press_release(self, ble_client):
        """Keyboard press and release over BLE."""
        # Press 'a' (keycode 0x04)
        payload = bytes([0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00])
        frame = build_ch9329(0x00, CMD_KB_GENERAL, payload)
        resp = ble_client.send_and_collect(frame)
        parsed = parse_ch9329(resp)
        assert parsed is not None, f"No response for keyboard press"
        assert parsed["cmd"] == (CMD_KB_GENERAL | RESP_OFFSET)
        assert parsed["payload"][0] == 0x00  # STATUS_SUCCESS

        # Release
        payload = bytes([0x00] * 8)
        frame = build_ch9329(0x00, CMD_KB_GENERAL, payload)
        resp = ble_client.send_and_collect(frame)
        parsed = parse_ch9329(resp)
        assert parsed is not None
        assert parsed["payload"][0] == 0x00

    def test_keyboard_with_modifier(self, ble_client):
        """Ctrl+A over BLE."""
        payload = bytes([0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00])
        frame = build_ch9329(0x00, CMD_KB_GENERAL, payload)
        resp = ble_client.send_and_collect(frame)
        parsed = parse_ch9329(resp)
        assert parsed is not None
        assert parsed["cmd"] == (CMD_KB_GENERAL | RESP_OFFSET)
        assert parsed["payload"][0] == 0x00

        # Release
        payload = bytes([0x00] * 8)
        frame = build_ch9329(0x00, CMD_KB_GENERAL, payload)
        ble_client.send_and_collect(frame)

    def test_absolute_mouse(self, ble_client):
        """Absolute mouse move (0x04) over BLE — 7-byte payload."""
        payload = bytes([0x00, 0x00, 0xF4, 0x01, 0x2C, 0x01, 0x00])
        frame = build_ch9329(0x00, CMD_MS_ABS, payload)
        resp = ble_client.send_and_collect(frame)
        parsed = parse_ch9329(resp)
        assert parsed is not None, f"No response for absolute mouse"
        assert parsed["cmd"] == (CMD_MS_ABS | RESP_OFFSET)
        assert parsed["payload"][0] == 0x00

    def test_relative_mouse(self, ble_client):
        """Relative mouse move (0x05) over BLE."""
        payload = bytes([0x00, 0x00, 10, 20, 0x00])
        frame = build_ch9329(0x00, CMD_MS_REL, payload)
        resp = ble_client.send_and_collect(frame)
        parsed = parse_ch9329(resp)
        assert parsed is not None
        assert parsed["cmd"] == (CMD_MS_REL | RESP_OFFSET)
        assert parsed["payload"][0] == 0x00

    def test_relative_mouse_wheel(self, ble_client):
        """Relative mouse scroll wheel over BLE."""
        payload = bytes([0x00, 0x00, 0x00, 0x00, 0x03])
        frame = build_ch9329(0x00, CMD_MS_REL, payload)
        resp = ble_client.send_and_collect(frame)
        parsed = parse_ch9329(resp)
        assert parsed is not None
        assert parsed["cmd"] == (CMD_MS_REL | RESP_OFFSET)

    def test_media_key(self, ble_client):
        """Media key (0x03) over BLE."""
        # Volume Up
        payload = bytes([0x02, 0x01, 0x00, 0x00])
        frame = build_ch9329(0x00, CMD_KB_MEDIA, payload)
        resp = ble_client.send_and_collect(frame)
        parsed = parse_ch9329(resp)
        assert parsed is not None
        assert parsed["cmd"] == (CMD_KB_MEDIA | RESP_OFFSET)
        assert parsed["payload"][0] == 0x00

        # Release
        payload = bytes([0x02, 0x00, 0x00, 0x00])
        frame = build_ch9329(0x00, CMD_KB_MEDIA, payload)
        ble_client.send_and_collect(frame)

    def test_get_para_cfg(self, ble_client):
        """Get parameter config (0x08) over BLE."""
        frame = build_ch9329(0x00, CMD_GET_PARA_CFG)
        resp = ble_client.send_and_collect(frame)
        parsed = parse_ch9329(resp)
        assert parsed is not None
        assert parsed["cmd"] == (CMD_GET_PARA_CFG | RESP_OFFSET)
        assert len(parsed["payload"]) == 50

    def test_usb_work_mode_read(self, ble_client):
        """USB work mode read (0x30) over BLE."""
        frame = build_ch9329(0x00, CMD_USB_WORK_MODE)
        resp = ble_client.send_and_collect(frame, timeout=3.0)
        parsed = parse_ch9329(resp)
        assert parsed is not None, f"No response for USB work mode read"
        expected_ack = CMD_USB_WORK_MODE | RESP_OFFSET
        expected_err = CMD_USB_WORK_MODE | 0xC0
        assert parsed["cmd"] in (expected_ack, expected_err), (
            f"Unexpected cmd 0x{parsed['cmd']:02X}"
        )
        if parsed["cmd"] == expected_ack and len(parsed["payload"]) >= 12:
            assert parsed["payload"][0] == 0x00

    def test_rapid_keyboard_sequence(self, ble_client):
        """Rapid key press/release sequence over BLE."""
        for key in [0x04, 0x05, 0x06, 0x07]:  # a, b, c, d
            # Press
            payload = bytes([0x00, 0x00, key, 0x00, 0x00, 0x00, 0x00, 0x00])
            frame = build_ch9329(0x00, CMD_KB_GENERAL, payload)
            resp = ble_client.send_and_collect(frame, timeout=2.0)
            parsed = parse_ch9329(resp)
            assert parsed is not None, f"No response for key 0x{key:02X}"
            assert parsed["payload"][0] == 0x00

            # Release
            payload = bytes([0x00] * 8)
            frame = build_ch9329(0x00, CMD_KB_GENERAL, payload)
            ble_client.send_and_collect(frame, timeout=2.0)

    def test_invalid_command(self, ble_client):
        """Invalid command should return error or be rejected."""
        frame = build_ch9329(0x00, 0xFF)
        resp = ble_client.send_and_collect(frame)
        parsed = parse_ch9329(resp)
        assert parsed is not None
        # 0xFF is not a CH9329 command, so it falls through to BLE-Eth parser
        # which returns 0xD0 (unknown BLE-Eth command). Either error is acceptable.
        ch9329_err = 0xFF | 0xC0
        ble_eth_err = 0xD0
        assert parsed["cmd"] in (ch9329_err, ble_eth_err), (
            f"Unexpected cmd 0x{parsed['cmd']:02X}"
        )

    def test_bad_checksum_rejected(self, ble_client):
        """Frame with bad checksum should be silently dropped."""
        frame = build_ch9329(0x00, CMD_GET_INFO)
        # Corrupt the checksum
        frame = frame[:-1] + bytes([(frame[-1] + 1) & 0xFF])
        resp = ble_client.send_and_collect(frame, timeout=2.0)
        # No valid response expected
        parsed = parse_ch9329(resp)
        assert parsed is None or len(resp) == 0

    def test_keyboard_wrong_length(self, ble_client):
        """Keyboard command with wrong payload length returns error."""
        payload = bytes([0x00, 0x00, 0x04])  # Only 3 bytes, need 8
        frame = build_ch9329(0x00, CMD_KB_GENERAL, payload)
        resp = ble_client.send_and_collect(frame)
        parsed = parse_ch9329(resp)
        assert parsed is not None
        assert parsed["cmd"] == (CMD_KB_GENERAL | 0xC0)  # Error
        assert parsed["payload"][0] == 0xE5  # STATUS_ERR_PARAM
