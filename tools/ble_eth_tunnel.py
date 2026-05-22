"""BLE-to-Ethernet tunneling protocol — Python implementation.

Matches the firmware implementation in APP/ble_eth_protocol.c and
APP/include/ble_eth_protocol.h.  Uses the Bleak library for BLE
communication.

Protocol frame format (CH9329-compatible):

    +------+------+-------+------+--------+----------+------+
    | 57   | AB   | Addr  | Cmd  | PayLen | Payload  | CSum |
    +------+------+-------+------+--------+----------+------+

Commands:
    0x10  CONNECT       – establish TCP tunnel (ip:port)
    0x11  DATA          – fragmented TCP payload
    0x12  DISCONNECT    – close tunnel
    0x1F  INFO          – device capabilities

Responses use cmd | 0x80 (success) or | 0xC0 (error).
"""

from __future__ import annotations

import asyncio
import logging
import struct
from collections import deque
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Callable, Optional

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Constants (must match ble_eth_protocol.h)
# ---------------------------------------------------------------------------
FRAME_HEAD1 = 0x57
FRAME_HEAD2 = 0xAB
FRAME_HEAD_LEN = 5
MAX_PAYLOAD_LEN = 249

CMD_CONNECT = 0x10
CMD_DATA = 0x11
CMD_DISCONNECT = 0x12
CMD_INFO = 0x1F
CMD_PING = 0x1E

CMD_CONNECT_RESP = 0x90
CMD_DATA_RESP = 0x91
CMD_DATA_PUSH = 0x91
CMD_DISCONN_RESP = 0x92
CMD_CONN_CLOSED = 0xD2
CMD_INFO_RESP = 0x9F
CMD_PONG = 0x9E
CMD_ERROR = 0xD0

STATUS_SUCCESS = 0x00
STATUS_PARAM_ERR = 0xE5
STATUS_CONN_FULL = 0xE7

FRAG_MORE = 0x80
FRAG_FIRST = 0x40
FRAG_COUNT_MASK = 0x0F
FRAG_HEADER_LEN = 3

MAX_FRAG_DATA = MAX_PAYLOAD_LEN - FRAG_HEADER_LEN  # 249
MAX_REASSEMBLED = MAX_FRAG_DATA * 15  # ~3.7 KB

# BLE GATT UUIDs (16-bit, used with Nordic UUID base)
SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb"
TX_CHAR_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"   # notify (device → host)
RX_CHAR_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"   # write  (host → device)


# ---------------------------------------------------------------------------
# Frame builder
# ---------------------------------------------------------------------------

def _checksum(data: bytes) -> int:
    return sum(data) & 0xFF


def build_frame(addr: int, cmd: int, payload: bytes = b"") -> bytes:
    """Build a BLE-Eth frame."""
    frame = bytes([FRAME_HEAD1, FRAME_HEAD2, addr & 0xFF, cmd & 0xFF, len(payload)])
    frame += payload
    frame += bytes([_checksum(frame)])
    return frame


def build_connect(remote_ip: str, remote_port: int) -> bytes:
    """Build CONNECT frame."""
    ip_bytes = bytes(int(x) for x in remote_ip.split("."))
    payload = ip_bytes + struct.pack("!H", remote_port)
    return build_frame(0x00, CMD_CONNECT, payload)


def build_disconnect(conn_id: int) -> bytes:
    """Build DISCONNECT frame."""
    return build_frame(0x00, CMD_DISCONNECT, bytes([conn_id]))


def build_data_single(conn_id: int, data: bytes) -> Optional[bytes]:
    """Build a single (unfragmented) DATA frame. Returns None if too large."""
    if len(data) > MAX_FRAG_DATA:
        return None
    payload = bytes([FRAG_FIRST, 0x00, conn_id]) + data
    return build_frame(0x00, CMD_DATA, payload)


def build_data_fragmented(conn_id: int, data: bytes) -> list[bytes]:
    """Build fragmented DATA frames. Returns list of frames."""
    total_frags = (len(data) + MAX_FRAG_DATA - 1) // MAX_FRAG_DATA
    if total_frags > 15:
        raise ValueError(f"Data too large: {len(data)} bytes (> {MAX_REASSEMBLED})")

    frames = []
    offset = 0
    for seq in range(total_frags):
        chunk = data[offset:offset + MAX_FRAG_DATA]
        offset += len(chunk)

        flags = total_frags & FRAG_COUNT_MASK
        if seq == 0:
            flags |= FRAG_FIRST
        if seq < total_frags - 1:
            flags |= FRAG_MORE

        payload = bytes([flags, seq, conn_id]) + chunk
        frames.append(build_frame(0x00, CMD_DATA, payload))

    return frames


def build_info_request() -> bytes:
    """Build INFO request frame."""
    return build_frame(0x00, CMD_INFO, b"")


def build_ping(data: bytes = b"") -> bytes:
    """Build PING request frame (echo test, no network needed)."""
    return build_frame(0x00, CMD_PING, data)


# ---------------------------------------------------------------------------
# Frame parser
# ---------------------------------------------------------------------------

@dataclass
class ParsedFrame:
    addr: int
    cmd: int
    payload: bytes


class FrameParser:
    """State machine parser for BLE-Eth frames.

    Feed raw bytes via feed(); completed frames are placed in the
    received_frames deque.
    """

    def __init__(self, lenient: bool = False):
        self._state = 0  # 0..6 = wait head1, head2, addr, cmd, len, payload, csum
        self._buf = bytearray()
        self._payload_len = 0
        self._checksum = 0
        self._lenient = lenient
        self.received_frames: deque[ParsedFrame] = deque()

    def feed(self, data: bytes):
        for byte in data:
            self._process(byte)

    def _process(self, b: int):
        if self._state == 0:  # wait head1
            if b == FRAME_HEAD1:
                self._buf = bytearray([b])
                self._checksum = b
                self._state = 1
        elif self._state == 1:  # wait head2
            if b == FRAME_HEAD2:
                self._buf.append(b)
                self._checksum += b
                self._state = 2
            else:
                if b == FRAME_HEAD1:
                    self._buf = bytearray([b])
                    self._checksum = b
                    self._state = 1
                else:
                    self._state = 0
        elif self._state == 2:  # wait addr
            self._buf.append(b)
            self._checksum += b
            self._state = 3
        elif self._state == 3:  # wait cmd
            self._buf.append(b)
            self._checksum += b
            self._state = 4
        elif self._state == 4:  # wait len
            self._buf.append(b)
            self._checksum += b
            self._payload_len = b
            if self._payload_len == 0:
                self._state = 6  # skip to checksum
            elif self._payload_len > MAX_PAYLOAD_LEN:
                self._state = 0  # invalid, resync
            else:
                self._state = 5
        elif self._state == 5:  # wait payload
            self._buf.append(b)
            self._checksum += b
            if len(self._buf) >= FRAME_HEAD_LEN + self._payload_len:
                self._state = 6
        elif self._state == 6:  # wait checksum
            expected = self._checksum & 0xFF
            if b == expected:
                frame = ParsedFrame(
                    addr=self._buf[2],
                    cmd=self._buf[3],
                    payload=bytes(self._buf[5:5 + self._payload_len]),
                )
                self.received_frames.append(frame)
                logger.debug(f"Frame parsed: cmd=0x{self._buf[3]:02X} len={self._payload_len}")
            elif self._lenient:
                logger.warning(f"Checksum mismatch: expected 0x{expected:02X}, got 0x{b:02X} (accepting anyway)")
                frame = ParsedFrame(
                    addr=self._buf[2],
                    cmd=self._buf[3],
                    payload=bytes(self._buf[5:5 + self._payload_len]),
                )
                self.received_frames.append(frame)
            else:
                logger.warning(f"Checksum mismatch: expected 0x{self._checksum:02X}, got 0x{b:02X}")
            self._state = 0


# ---------------------------------------------------------------------------
# Reassembler
# ---------------------------------------------------------------------------

@dataclass
class ReassembledData:
    conn_id: int
    data: bytes


class DataReassembler:
    """Reassembles fragmented DATA frames into complete payloads."""

    def __init__(self):
        self._active = False
        self._conn_id = 0
        self._total_frags = 0
        self._next_seq = 0
        self._buf = bytearray()

    def feed(self, payload: bytes) -> Optional[ReassembledData]:
        """Process a DATA frame payload (including 3-byte fragment header).
        Returns ReassembledData when reassembly is complete, None otherwise.
        """
        if len(payload) < FRAG_HEADER_LEN:
            return None

        flags = payload[0]
        seq = payload[1]
        conn_id = payload[2]
        frag_data = payload[FRAG_HEADER_LEN:]

        total_frags = flags & FRAG_COUNT_MASK
        if total_frags == 0:
            total_frags = 1

        if flags & FRAG_FIRST:
            self._active = True
            self._conn_id = conn_id
            self._total_frags = total_frags
            self._next_seq = 0
            self._buf = bytearray()

        if not self._active:
            return None

        if seq != self._next_seq:
            logger.warning(f"Out of order fragment: expected {self._next_seq}, got {seq}")
            self._active = False
            return None

        self._buf.extend(frag_data)
        self._next_seq += 1

        if not (flags & FRAG_MORE):
            # Last fragment
            self._active = False
            return ReassembledData(conn_id=self._conn_id, data=bytes(self._buf))

        return None


# ---------------------------------------------------------------------------
# High-level BLE-Eth tunnel client
# ---------------------------------------------------------------------------

class ConnectionState(IntEnum):
    DISCONNECTED = 0
    CONNECTING = 1
    CONNECTED = 2
    ERROR = 3


@dataclass
class TunnelEvent:
    """Event from the tunnel to the application."""
    class Type(IntEnum):
        CONNECTED = 0
        DISCONNECTED = 1
        DATA_RECEIVED = 2
        ERROR = 3

    type: Type
    conn_id: int = 0
    data: bytes = b""
    error_msg: str = ""


class BleEthClient:
    """High-level BLE-Eth tunnel client.

    Usage::

        client = BleEthClient("KVM-GO")
        async with client.connect():
            conn_id = await client.open_tunnel("192.168.1.100", 22)
            await client.send_data(conn_id, b"hello")
            data = await client.recv_data(conn_id)
    """

    def __init__(
        self,
        device_name: str = "KVM-GO",
        ble_address: Optional[str] = None,
        max_retries: int = 3,
    ):
        self.device_name = device_name
        self.ble_address = ble_address
        self.max_retries = max_retries

        self._bleak_client = None
        self._parser = FrameParser(lenient=True)
        self._reassembler = DataReassembler()

        # Event queues keyed by conn_id
        self._data_queues: dict[int, asyncio.Queue] = {}
        self._connect_futures: dict[int, asyncio.Future] = {}
        self._disconnect_futures: dict[int, asyncio.Future] = {}

        # State
        self._state = ConnectionState.DISCONNECTED
        self._connected_event = asyncio.Event()
        self._event_callbacks: list[Callable[[TunnelEvent], None]] = []

        # Notification buffer accumulator
        self._notify_buffer = bytearray()

        # Info response holder (separate from the main frame queue)
        self._info_response: Optional[dict] = None
        self._info_event: Optional[asyncio.Event] = None

        # Ping response holder
        self._pong_response: Optional[bytes] = None
        self._pong_event: Optional[asyncio.Event] = None

    async def connect(self) -> "BleEthClient":
        """Connect to the BLE device."""
        from bleak import BleakClient

        # Find device
        from bleak import BleakScanner

        if self.ble_address:
            device = await BleakScanner.find_device_by_address(
                self.ble_address, timeout=10
            )
        else:
            devices = await BleakScanner.discover(timeout=5)
            device = None
            for d in devices:
                if d.name and self.device_name.lower() in d.name.lower():
                    device = d
                    break

            if device is None:
                # Try by address pattern
                for d in devices:
                    if d.name:
                        logger.info(f"Found BLE device: {d.name} ({d.address})")

        if device is None:
            raise ConnectionError(
                f"BLE device '{self.device_name}' not found. "
                "Make sure it is advertising and nearby."
            )

        logger.info(f"Connecting to {device.name} ({device.address})...")
        self._bleak_client = BleakClient(
            device,
            services=[SERVICE_UUID],
        )
        await self._bleak_client.connect()
        logger.info("BLE connected.")

        # Subscribe to TX characteristic notifications
        await self._bleak_client.start_notify(TX_CHAR_UUID, self._notification_handler)

        self._state = ConnectionState.CONNECTED
        self._connected_event.set()

        return self

    async def disconnect(self):
        """Disconnect from the BLE device."""
        if self._bleak_client and self._bleak_client.is_connected:
            try:
                await self._bleak_client.stop_notify(TX_CHAR_UUID)
            except Exception:
                pass
            await self._bleak_client.disconnect()
        self._state = ConnectionState.DISCONNECTED
        logger.info("BLE disconnected.")

    async def __aenter__(self):
        await self.connect()
        return self

    async def __aexit__(self, *args):
        await self.disconnect()

    async def get_info(self) -> dict:
        """Get device info/capabilities."""
        self._info_response = None
        self._info_event = asyncio.Event()

        await self._send_frame(build_info_request())

        # Wait for response (up to 5 seconds)
        try:
            await asyncio.wait_for(self._info_event.wait(), timeout=5.0)
        except asyncio.TimeoutError:
            raise TimeoutError("No info response received")

        info = self._info_response
        self._info_response = None
        self._info_event = None
        return info

    async def ping(self, data: bytes = b"hello", timeout: float = 5.0) -> bytes:
        """Send a PING and wait for PONG echo response.
        Tests the BLE data path without requiring network connectivity.
        """
        self._pong_response = None
        self._pong_event = asyncio.Event()

        await self._send_frame(build_ping(data))

        try:
            await asyncio.wait_for(self._pong_event.wait(), timeout=timeout)
        except asyncio.TimeoutError:
            raise TimeoutError("No PONG response received")

        pong = self._pong_response
        self._pong_response = None
        self._pong_event = None
        return pong

    async def open_tunnel(
        self, remote_ip: str, remote_port: int, timeout: float = 10.0
    ) -> int:
        """Open a TCP tunnel to remote_ip:remote_port.
        Returns the connection ID.
        """
        conn_id_future = asyncio.get_event_loop().create_future()
        self._connect_futures["pending"] = conn_id_future

        await self._send_frame(build_connect(remote_ip, remote_port))

        try:
            result = await asyncio.wait_for(conn_id_future, timeout=timeout)
        except asyncio.TimeoutError:
            self._connect_futures.pop("pending", None)
            raise TimeoutError(f"CONNECT to {remote_ip}:{remote_port} timed out")

        if result["status"] != STATUS_SUCCESS:
            raise ConnectionError(
                f"CONNECT failed: status=0x{result['status']:02X}"
            )

        conn_id = result["conn_id"]
        self._data_queues[conn_id] = asyncio.Queue()
        logger.info(f"Tunnel opened: conn_id={conn_id} -> {remote_ip}:{remote_port}")
        return conn_id

    async def close_tunnel(self, conn_id: int, timeout: float = 5.0):
        """Close a TCP tunnel."""
        disc_future = asyncio.get_event_loop().create_future()
        self._disconnect_futures[conn_id] = disc_future

        await self._send_frame(build_disconnect(conn_id))

        try:
            await asyncio.wait_for(disc_future, timeout=timeout)
        except asyncio.TimeoutError:
            logger.warning(f"DISCONNECT timeout for conn_id={conn_id}")

        self._data_queues.pop(conn_id, None)
        self._disconnect_futures.pop(conn_id, None)
        logger.info(f"Tunnel closed: conn_id={conn_id}")

    async def send_data(self, conn_id: int, data: bytes):
        """Send data through the tunnel (auto-fragmentation)."""
        if len(data) <= MAX_FRAG_DATA:
            frame = build_data_single(conn_id, data)
            if frame:
                await self._send_frame(frame)
        else:
            frames = build_data_fragmented(conn_id, data)
            for frame in frames:
                await self._send_frame(frame)

    async def recv_data(self, conn_id: int, timeout: float = 30.0) -> bytes:
        """Receive data from the tunnel."""
        queue = self._data_queues.get(conn_id)
        if queue is None:
            raise ValueError(f"No tunnel for conn_id={conn_id}")
        data = await asyncio.wait_for(queue.get(), timeout=timeout)
        return data

    def add_event_callback(self, callback: Callable[[TunnelEvent], None]):
        """Register a callback for tunnel events."""
        self._event_callbacks.append(callback)

    def _emit_event(self, event: TunnelEvent):
        for cb in self._event_callbacks:
            try:
                cb(event)
            except Exception as e:
                logger.error(f"Event callback error: {e}")

    async def _send_frame(self, frame: bytes):
        """Send a frame to the device via BLE write."""
        if not self._bleak_client or not self._bleak_client.is_connected:
            raise ConnectionError("Not connected to BLE device")
        logger.debug(f"TX frame: {frame.hex(' ')}")
        await self._bleak_client.write_gatt_char(RX_CHAR_UUID, frame, response=False)

    def _notification_handler(self, sender, data: bytes):
        """Handle BLE notifications (device → host)."""
        logger.debug(f"RX BLE data ({len(data)} bytes): {bytes(data).hex(' ')}")
        self._parser.feed(data)

        # Process all parsed frames
        while self._parser.received_frames:
            frame = self._parser.received_frames.popleft()
            self._handle_frame(frame)

    def _handle_frame(self, frame: ParsedFrame):
        """Process a parsed frame."""
        cmd = frame.cmd
        payload = frame.payload

        if cmd == CMD_CONNECT_RESP:
            # Connect response
            if len(payload) >= 2:
                conn_id = payload[0]
                status = payload[1]
                future = self._connect_futures.pop("pending", None)
                if future:
                    future.set_result({"conn_id": conn_id, "status": status})
                self._emit_event(TunnelEvent(
                    type=TunnelEvent.Type.CONNECTED,
                    conn_id=conn_id,
                    error_msg=f"status=0x{status:02X}" if status != STATUS_SUCCESS else "",
                ))

        elif cmd == CMD_DATA_PUSH:
            # Incoming data (pushed from device)
            result = self._reassembler.feed(payload)
            if result:
                queue = self._data_queues.get(result.conn_id)
                if queue:
                    queue.put_nowait(result.data)
                self._emit_event(TunnelEvent(
                    type=TunnelEvent.Type.DATA_RECEIVED,
                    conn_id=result.conn_id,
                    data=result.data,
                ))

        elif cmd == CMD_DATA_RESP:
            # Data acknowledged
            logger.debug("DATA ACK received")

        elif cmd == CMD_DISCONN_RESP:
            # Disconnect response
            if len(payload) >= 2:
                conn_id = payload[0]
                future = self._disconnect_futures.pop(conn_id, None)
                if future:
                    future.set_result(True)
                self._emit_event(TunnelEvent(
                    type=TunnelEvent.Type.DISCONNECTED,
                    conn_id=conn_id,
                ))

        elif cmd == CMD_CONN_CLOSED:
            # Remote closed
            if len(payload) >= 1:
                conn_id = payload[0]
                self._emit_event(TunnelEvent(
                    type=TunnelEvent.Type.DISCONNECTED,
                    conn_id=conn_id,
                ))

        elif cmd == CMD_INFO_RESP:
            if len(payload) >= 8:
                info = {
                    "version": payload[0],
                    "max_connections": payload[1],
                    "max_fragment_payload": (payload[2] << 8) | payload[3],
                    "features": payload[4],
                    "raw": payload,
                }
                self._info_response = info
                if self._info_event:
                    self._info_event.set()
            logger.info(f"Info response: {payload.hex(' ')}")

        elif cmd == CMD_PONG:
            logger.info(f"PONG received ({len(payload)} bytes): {payload.hex(' ')}")
            self._pong_response = payload
            if self._pong_event:
                self._pong_event.set()

        elif cmd == CMD_ERROR:
            self._emit_event(TunnelEvent(
                type=TunnelEvent.Type.ERROR,
                error_msg="Device returned error",
            ))


# ---------------------------------------------------------------------------
# BLE TCP proxy — bridges local TCP sockets through BLE tunnel
# ---------------------------------------------------------------------------

class BleTcpProxy:
    """Local TCP proxy that forwards connections through a BLE-Eth tunnel.

    Usage::

        proxy = BleTcpProxy(client, local_port=8022, target_ip="192.168.1.100", target_port=22)
        await proxy.start()
        # Now connect to localhost:8022 → forwarded through BLE → 192.168.1.100:22
        await proxy.stop()
    """

    def __init__(
        self,
        client: BleEthClient,
        local_port: int = 8022,
        target_ip: str = "192.168.1.100",
        target_port: int = 22,
        local_host: str = "127.0.0.1",
        recv_timeout: float = 30.0,
    ):
        self.client = client
        self.local_port = local_port
        self.target_ip = target_ip
        self.target_port = target_port
        self.local_host = local_host
        self.recv_timeout = recv_timeout

        self._server: Optional[asyncio.Server] = None
        self._active_connections: dict[asyncio.StreamWriter, int] = {}

    async def start(self):
        """Start the local TCP proxy server."""
        self._server = await asyncio.start_server(
            self._handle_local_connection,
            self.local_host,
            self.local_port,
        )
        addr = self._server.sockets[0].getsockname()
        logger.info(f"BLE TCP proxy listening on {addr[0]}:{addr[1]}")
        logger.info(f"  -> tunnel target: {self.target_ip}:{self.target_port}")

    async def stop(self):
        """Stop the proxy server and close all active connections."""
        for writer in list(self._active_connections):
            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass
        self._active_connections.clear()

        if self._server:
            self._server.close()
            await self._server.wait_closed()
            self._server = None
        logger.info("BLE TCP proxy stopped.")

    async def _handle_local_connection(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ):
        """Handle a new local TCP connection."""
        peer = writer.get_extra_info("peername")
        logger.info(f"Local connection from {peer}")

        conn_id = -1
        try:
            # Open BLE tunnel
            conn_id = await self.client.open_tunnel(self.target_ip, self.target_port)
            self._active_connections[writer] = conn_id

            # Bidirectional forwarding
            local_to_ble = asyncio.create_task(
                self._forward_local_to_ble(reader, conn_id)
            )
            ble_to_local = asyncio.create_task(
                self._forward_ble_to_local(writer, conn_id)
            )

            done, pending = await asyncio.wait(
                [local_to_ble, ble_to_local],
                return_when=asyncio.FIRST_COMPLETED,
            )

            for task in pending:
                task.cancel()
                try:
                    await task
                except asyncio.CancelledError:
                    pass

        except Exception as e:
            logger.error(f"Proxy error: {e}")
            try:
                writer.close()
            except Exception:
                pass
        finally:
            if conn_id >= 0:
                try:
                    await self.client.close_tunnel(conn_id)
                except Exception:
                    pass
            self._active_connections.pop(writer, None)
            logger.info(f"Local connection from {peer} closed.")

    async def _forward_local_to_ble(
        self, reader: asyncio.StreamReader, conn_id: int
    ):
        """Forward data from local TCP -> BLE tunnel."""
        try:
            while True:
                data = await reader.read(4096)
                if not data:
                    break
                await self.client.send_data(conn_id, data)
                logger.debug(f"Local->BLE: {len(data)} bytes")
        except (ConnectionResetError, BrokenPipeError):
            pass

    async def _forward_ble_to_local(
        self, writer: asyncio.StreamWriter, conn_id: int
    ):
        """Forward data from BLE tunnel -> local TCP."""
        try:
            while True:
                data = await self.client.recv_data(conn_id, timeout=self.recv_timeout)
                if not data:
                    break
                writer.write(data)
                await writer.drain()
                logger.debug(f"BLE->Local: {len(data)} bytes")
        except asyncio.TimeoutError:
            logger.warning(f"BLE recv timeout for conn_id={conn_id}")
        except (ConnectionResetError, BrokenPipeError):
            pass
