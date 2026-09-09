#!/usr/bin/env python3
"""
CH9329 Protocol Fuzzer - Serial Port Maximum-Rate Stress Test

Saturates the serial link at wire speed, tracks response rate for both
keyboard and mouse commands separately.

Wire-speed calculation (8N1, 10 bits per byte):
  115200 baud → 11520 bytes/sec
  921600 baud → 92160 bytes/sec

Usage:
    python3 serial_fuzz_test.py                          # auto-detect, 115200, 60s
    python3 serial_fuzz_test.py -b 921600 -t 120         # 921600 baud, 120s
    python3 serial_fuzz_test.py /dev/ttyUSB0 115200      # explicit port+baud
    python3 serial_fuzz_test.py --list                    # list ports

Author: Security Test Script
Date: 2026-09-09
"""

import serial
import sys
import time
import random
import argparse
import threading
from collections import defaultdict
from typing import Optional, Dict
from serial.tools import list_ports

# ─── CH9329 Protocol Constants ───────────────────────────────────────────────
CH9329_FRAME_HEAD1 = 0x57
CH9329_FRAME_HEAD2 = 0xAB
CH9329_MAX_DATA_LEN = 100

CMD_GET_INFO             = 0x01
CMD_SEND_KB_GENERAL_DATA = 0x02
CMD_SEND_KB_MEDIA_DATA   = 0x03
CMD_SEND_MS_ABS_DATA     = 0x04
CMD_SEND_MS_REL_DATA     = 0x05

# Response command codes (with 0x80 or 0xC0 flag)
RESP_CMD_MASK = 0x7F  # Mask to extract base command from response

# ─── Packet builders ─────────────────────────────────────────────────────────

def _checksum(data: bytes) -> int:
    return sum(data) & 0xFF

def build_valid_kb_packet() -> bytes:
    """Valid keyboard packet: 15 bytes on wire"""
    data = bytes([0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00])
    pkt = bytes([CH9329_FRAME_HEAD1, CH9329_FRAME_HEAD2, 0x01, CMD_SEND_KB_GENERAL_DATA, 8]) + data
    return pkt + bytes([_checksum(pkt)])

def build_valid_mouse_packet() -> bytes:
    """Valid relative mouse packet: 12 bytes on wire"""
    data = bytes([0x00, 0x01, 0x0A, 0x0A, 0x00])
    pkt = bytes([CH9329_FRAME_HEAD1, CH9329_FRAME_HEAD2, 0x01, CMD_SEND_MS_REL_DATA, 5]) + data
    return pkt + bytes([_checksum(pkt)])

def build_oversized_packet(data_len: int) -> bytes:
    """Packet with data_len > CH9329_MAX_DATA_LEN (should be rejected)"""
    pkt = bytes([CH9329_FRAME_HEAD1, CH9329_FRAME_HEAD2, 0x01, CMD_SEND_KB_GENERAL_DATA, data_len])
    pkt += bytes([0x00] * data_len)
    return pkt + bytes([_checksum(pkt)])

def build_boundary_packet(data_len: int) -> bytes:
    """Packet at exact boundary (100 = max valid, 101 = min invalid)"""
    cmd = CMD_SEND_KB_GENERAL_DATA
    pkt = bytes([CH9329_FRAME_HEAD1, CH9329_FRAME_HEAD2, 0x01, cmd, data_len])
    pkt += bytes([0x00] * data_len)
    return pkt + bytes([_checksum(pkt)])

def build_bad_header_packet() -> bytes:
    """Packet with wrong frame header (should be skipped)"""
    pkt = bytes([0x00, 0xFF, 0x01, CMD_SEND_KB_GENERAL_DATA, 8])
    pkt += bytes([0x00] * 8)
    return pkt + bytes([_checksum(pkt)])

def build_random_packet() -> bytes:
    """Completely random bytes (1..300 bytes)"""
    size = random.randint(1, 300)
    return bytes([random.randint(0, 255) for _ in range(size)])

def build_zero_len_packet() -> bytes:
    """Packet claiming data_len = 0"""
    pkt = bytes([CH9329_FRAME_HEAD1, CH9329_FRAME_HEAD2, 0x01, CMD_SEND_KB_GENERAL_DATA, 0])
    return pkt + bytes([_checksum(pkt)])

def build_mismatch_len_packet() -> bytes:
    """Packet claiming data_len=100 but only 10 bytes of payload"""
    pkt = bytes([CH9329_FRAME_HEAD1, CH9329_FRAME_HEAD2, 0x01, CMD_SEND_KB_GENERAL_DATA, 100])
    pkt += bytes([0x00] * 10)
    return pkt + bytes([_checksum(pkt)])

# ─── Pre-build packet pools ──────────────────────────────────────────────────

def build_packet_pools():
    """Pre-build pools of each packet type for wire-speed sending"""
    valid_kb     = [build_valid_kb_packet() for _ in range(256)]
    valid_mouse  = [build_valid_mouse_packet() for _ in range(256)]
    oversized    = [build_oversized_packet(dl) for dl in [101, 120, 150, 200, 255] for _ in range(50)]
    boundary     = [build_boundary_packet(100), build_boundary_packet(101)] * 128
    bad_header   = [build_bad_header_packet() for _ in range(256)]
    zero_len     = [build_zero_len_packet() for _ in range(256)]
    mismatch     = [build_mismatch_len_packet() for _ in range(256)]
    return {
        'valid_kb': valid_kb,
        'valid_mouse': valid_mouse,
        'oversized': oversized,
        'boundary': boundary,
        'bad_header': bad_header,
        'zero_len': zero_len,
        'mismatch': mismatch,
    }

# ─── Response parser and tracker ─────────────────────────────────────────────

class ResponseTracker:
    """Thread-safe response tracker with per-command statistics"""

    def __init__(self):
        self.lock = threading.Lock()
        self.sent = defaultdict(int)      # cmd -> count
        self.received = defaultdict(int)  # cmd -> count
        self.total_bytes_rx = 0
        self.parse_errors = 0

    def record_sent(self, cmd: int):
        with self.lock:
            self.sent[cmd] += 1

    def record_received(self, cmd: int):
        with self.lock:
            self.received[cmd] += 1

    def record_bytes_rx(self, n: int):
        with self.lock:
            self.total_bytes_rx += n

    def record_parse_error(self):
        with self.lock:
            self.parse_errors += 1

    def get_stats(self):
        with self.lock:
            return {
                'sent': dict(self.sent),
                'received': dict(self.received),
                'total_bytes_rx': self.total_bytes_rx,
                'parse_errors': self.parse_errors,
            }

    def parse_response(self, data: bytes) -> Optional[int]:
        """Parse CH9329 response frame, return command code or None"""
        if len(data) < 6:
            return None

        # Check frame header
        if data[0] != CH9329_FRAME_HEAD1 or data[1] != CH9329_FRAME_HEAD2:
            return None

        addr = data[2]
        cmd = data[3]
        data_len = data[4]

        # Check if this is a response (0x80 or 0xC0 flag)
        if not (cmd & 0x80):
            return None

        base_cmd = cmd & RESP_CMD_MASK

        # Verify frame length
        expected_len = 5 + data_len + 1  # header(5) + data + checksum(1)
        if len(data) < expected_len:
            return None

        # Verify checksum
        checksum = data[expected_len - 1]
        calculated = _checksum(data[:expected_len - 1])
        if checksum != calculated:
            self.record_parse_error()
            return None

        return base_cmd


class ResponseReceiver(threading.Thread):
    """Background thread that reads and parses responses from the serial port"""

    def __init__(self, ser: serial.Serial, tracker: ResponseTracker):
        super().__init__(daemon=True)
        self.ser = ser
        self.tracker = tracker
        self.running = True
        self.buffer = bytearray()

    def stop(self):
        self.running = False

    def run(self):
        while self.running:
            try:
                # Read available data
                data = self.ser.read(1024)
                if not data:
                    time.sleep(0.001)
                    continue

                self.tracker.record_bytes_rx(len(data))
                self.buffer.extend(data)

                # Parse complete frames from buffer
                while len(self.buffer) >= 6:
                    # Find frame header
                    header_pos = -1
                    for i in range(len(self.buffer) - 1):
                        if self.buffer[i] == CH9329_FRAME_HEAD1 and self.buffer[i+1] == CH9329_FRAME_HEAD2:
                            header_pos = i
                            break

                    if header_pos < 0:
                        # No header found, discard buffer
                        self.buffer.clear()
                        break

                    # Discard bytes before header
                    if header_pos > 0:
                        self.buffer = self.buffer[header_pos:]

                    # Need at least header + length field
                    if len(self.buffer) < 5:
                        break

                    data_len = self.buffer[4]
                    frame_len = 5 + data_len + 1

                    # Wait for complete frame
                    if len(self.buffer) < frame_len:
                        break

                    # Extract and parse frame
                    frame = bytes(self.buffer[:frame_len])
                    cmd = self.tracker.parse_response(frame)

                    if cmd is not None:
                        self.tracker.record_received(cmd)

                    # Remove parsed frame from buffer
                    self.buffer = self.buffer[frame_len:]

            except Exception as e:
                if self.running:
                    print(f"\n[Receiver error] {e}")
                break

# ─── Serial port auto-detect ─────────────────────────────────────────────────

def find_serial_port() -> Optional[str]:
    """Auto-detect serial port. Returns None if not found."""
    ports = list(list_ports.comports())
    if not ports:
        return None

    known_chips = ['ch340', 'ch341', 'cp210', 'ft232', 'ftdi', 'usb serial',
                   'usb-serial', 'usbmodem', 'wch', 'ch9102', 'ch932']

    scored = []
    for p in ports:
        desc = (p.description or '').lower()
        name = (p.name or '').lower()
        combined = desc + ' ' + name

        if any(c in combined for c in known_chips):
            scored.append((0, p.device, p.description))
        elif 'usb' in combined or 'serial' in combined or 'tty' in name:
            scored.append((1, p.device, p.description))
        else:
            scored.append((2, p.device, p.description))

    scored.sort(key=lambda x: x[0])

    if len(scored) == 1:
        return scored[0][1]

    best = [s for s in scored if s[0] == scored[0][0]]
    if len(best) == 1:
        return best[0][1]

    print("\nMultiple serial ports found:")
    for i, (_, dev, desc) in enumerate(scored):
        print(f"  [{i+1}] {dev}  ({desc})")
    print("  [q] Quit")

    while True:
        choice = input("\nSelect port (number): ").strip().lower()
        if choice == 'q':
            return None
        try:
            idx = int(choice) - 1
            if 0 <= idx < len(scored):
                return scored[idx][1]
        except ValueError:
            pass

# ─── Test phases ─────────────────────────────────────────────────────────────

WRITE_CHUNK = 4096  # bytes per ser.write() call

def phase_keyboard(ser, tracker, duration, pools, target_pps=0):
    """Phase 1: Keyboard packets only"""
    pool = pools['valid_kb']
    return _run_phase_with_response(ser, tracker, "Keyboard packets", pool, duration, CMD_SEND_KB_GENERAL_DATA, target_pps)

def phase_mouse(ser, tracker, duration, pools, target_pps=0):
    """Phase 2: Mouse packets only"""
    pool = pools['valid_mouse']
    return _run_phase_with_response(ser, tracker, "Mouse packets", pool, duration, CMD_SEND_MS_REL_DATA, target_pps)

def phase_mixed_kb_mouse(ser, tracker, duration, pools, target_pps=0):
    """Phase 3: Mixed keyboard + mouse (50/50)"""
    pool = pools['valid_kb'] + pools['valid_mouse']
    random.shuffle(pool)
    return _run_phase_with_response(ser, tracker, "Mixed KB + Mouse (50/50)", pool, duration,
                                     [CMD_SEND_KB_GENERAL_DATA, CMD_SEND_MS_REL_DATA], target_pps)

def phase_oversized(ser, tracker, duration, pools, target_pps=0):
    """Phase 4: 100% oversized packets (P0-2 boundary check)"""
    pool = pools['oversized']
    return _run_phase_with_response(ser, tracker, "Oversized data_len (P0-2)", pool, duration, CMD_SEND_KB_GENERAL_DATA, target_pps)

def phase_boundary(ser, tracker, duration, pools, target_pps=0):
    """Phase 5: Exact boundary values (100 valid / 101 invalid)"""
    pool = pools['boundary']
    return _run_phase_with_response(ser, tracker, "Boundary values (100/101)", pool, duration, CMD_SEND_KB_GENERAL_DATA, target_pps)

def phase_bad_header(ser, tracker, duration, pools, target_pps=0):
    """Phase 6: Invalid frame headers"""
    pool = pools['bad_header']
    return _run_phase_with_response(ser, tracker, "Invalid headers", pool, duration, CMD_SEND_KB_GENERAL_DATA, target_pps)

def phase_mixed_valid_malformed(ser, tracker, duration, pools, target_pps=0):
    """Phase 7: 50% valid + 50% malformed mix"""
    pool = (pools['valid_kb'] + pools['valid_mouse'] +
            pools['oversized'] + pools['bad_header'] +
            pools['zero_len'] + pools['mismatch'])
    random.shuffle(pool)
    return _run_phase_with_response(ser, tracker, "Mixed valid/malformed (50/50)", pool, duration,
                                     [CMD_SEND_KB_GENERAL_DATA, CMD_SEND_MS_REL_DATA], target_pps)

def phase_sustained(ser, tracker, duration, pools, target_pps=0):
    """Phase 8: Sustained wire-speed barrage (all types mixed)"""
    pool = (pools['valid_kb'] * 10 + pools['valid_mouse'] * 10 +
            pools['oversized'] * 4 + pools['bad_header'] * 4 +
            pools['zero_len'] * 2 + pools['mismatch'] * 2 +
            pools['boundary'] * 2)
    random.shuffle(pool)
    return _run_phase_with_response(ser, tracker, "Sustained wire-speed barrage", pool, duration,
                                     [CMD_SEND_KB_GENERAL_DATA, CMD_SEND_MS_REL_DATA], target_pps)

def _run_phase_with_response(ser, tracker, name, pool, duration, expected_cmds, target_pps=0):
    """Run one test phase, tracking responses. If target_pps > 0, limit sending rate."""
    print(f"\n{'='*60}")
    print(f"  {name}")
    if target_pps > 0:
        print(f"  Target rate: {target_pps} packets/s")
    print(f"{'='*60}")

    # Normalize expected_cmds to list
    if not isinstance(expected_cmds, list):
        expected_cmds = [expected_cmds]

    total_bytes_tx = 0
    total_packets_tx = 0
    idx = 0
    pool_len = len(pool)
    start = time.time()
    deadline = start + duration

    # Rate limiting
    next_send_time = start
    packet_interval = 1.0 / target_pps if target_pps > 0 else 0

    while time.time() < deadline:
        # Send one packet
        pkt = pool[idx % pool_len]
        ser.write(pkt)
        idx += 1
        total_packets_tx += 1
        total_bytes_tx += len(pkt)

        # Track what we're sending
        if len(pkt) >= 5:
            cmd = pkt[3]
            tracker.record_sent(cmd)

        # Rate limiting: sleep until next packet should be sent
        if target_pps > 0:
            next_send_time += packet_interval
            now = time.time()
            if next_send_time > now:
                time.sleep(next_send_time - now)
            else:
                # We're behind schedule, reset to now
                next_send_time = now

        # Progress every ~0.5s
        if total_packets_tx % 500 == 0:
            elapsed = time.time() - start
            stats = tracker.get_stats()
            rate_tx = total_bytes_tx / elapsed / 1024
            rate_rx = stats['total_bytes_rx'] / elapsed / 1024

            # Calculate response rate for expected commands
            total_expected = sum(stats['sent'].get(cmd, 0) for cmd in expected_cmds)
            total_received = sum(stats['received'].get(cmd, 0) for cmd in expected_cmds)
            resp_rate = (total_received / total_expected * 100) if total_expected > 0 else 0

            print(f"  {elapsed:6.1f}s | TX: {total_packets_tx:>8} pkts {rate_tx:7.1f} KB/s | "
                  f"RX: {total_received:>6} {rate_rx:6.1f} KB/s | Resp: {resp_rate:5.1f}%")

    elapsed = time.time() - start
    _print_phase_result_with_response(tracker, expected_cmds, total_bytes_tx, total_packets_tx, elapsed)
    return True

def _print_phase_result_with_response(tracker, expected_cmds, total_bytes_tx, total_packets_tx, elapsed):
    """Print phase results with response rate breakdown"""
    stats = tracker.get_stats()

    rate_tx = total_bytes_tx / elapsed / 1024 if elapsed > 0 else 0
    pps_tx = total_packets_tx / elapsed if elapsed > 0 else 0
    rate_rx = stats['total_bytes_rx'] / elapsed / 1024 if elapsed > 0 else 0

    print(f"  ─────────────────────────────────────")
    print(f"  TX: {total_bytes_tx:,} bytes | {total_packets_tx:,} packets | {elapsed:.1f}s")
    print(f"  TX Rate: {rate_tx:,.1f} KB/s | {pps_tx:,.0f} packets/s")
    print(f"  RX: {stats['total_bytes_rx']:,} bytes | {rate_rx:,.1f} KB/s")
    print(f"  Parse errors: {stats['parse_errors']}")

    # Per-command breakdown
    print(f"\n  Response rate by command:")
    total_expected = 0
    total_received = 0

    for cmd in expected_cmds:
        sent = stats['sent'].get(cmd, 0)
        received = stats['received'].get(cmd, 0)
        total_expected += sent
        total_received += received

        cmd_name = {
            CMD_SEND_KB_GENERAL_DATA: "Keyboard",
            CMD_SEND_MS_REL_DATA: "Mouse",
        }.get(cmd, f"CMD_{cmd:02x}")

        rate = (received / sent * 100) if sent > 0 else 0
        status = "✓" if rate > 90 else "⚠" if rate > 50 else "✗"
        print(f"    {status} {cmd_name:15s}: {received:>6} / {sent:>6} ({rate:5.1f}%)")

    overall_rate = (total_received / total_expected * 100) if total_expected > 0 else 0
    print(f"  ─────────────────────────────────────")
    print(f"  Overall response rate: {total_received} / {total_expected} ({overall_rate:.1f}%)")

# ─── Main ────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description='CH9329 Protocol Fuzzer — maximum-rate serial stress test with response tracking',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                                # auto-detect port, 115200, 60s per phase
  %(prog)s -b 921600 -t 120               # 921600 baud, 120s per phase
  %(prog)s /dev/ttyUSB0 115200 -t 30      # explicit port, 30s per phase
  %(prog)s --list                          # list available serial ports
  %(prog)s --phase 1                       # run only phase 1 (keyboard)
  %(prog)s --phase 3 -t 300               # mixed KB+mouse for 5 minutes

Wire-speed reference (8N1):
  115200 baud →  11,520 bytes/s → ~768 kb pkts/s or ~107 max-pkts/s
  460800 baud →  46,080 bytes/s
  921600 baud →  92,160 bytes/s
""")
    parser.add_argument('port', nargs='?', default=None,
                        help='Serial port (auto-detect if omitted)')
    parser.add_argument('baudrate', nargs='?', type=int, default=115200,
                        help='Baud rate (default: 115200)')
    parser.add_argument('-b', '--baud', type=int, default=None,
                        help='Baud rate (alternative to positional arg)')
    parser.add_argument('-t', '--time', type=float, default=60,
                        help='Duration per test phase in seconds (default: 60)')
    parser.add_argument('-p', '--phase', type=int, default=0,
                        help='Run a single phase (1-8), 0 = all phases')
    parser.add_argument('-r', '--rate', type=int, default=0,
                        help='Target send rate in packets/s (0 = wire speed, no limit)')
    parser.add_argument('--list', action='store_true',
                        help='List available serial ports and exit')

    args = parser.parse_args()

    if args.list:
        ports = list(list_ports.comports())
        if not ports:
            print("No serial ports found.")
            sys.exit(0)
        print(f"Found {len(ports)} serial port(s):\n")
        for p in sorted(ports, key=lambda x: x.device):
            print(f"  {p.device:20s} {p.description}")
        sys.exit(0)

    # Resolve baud rate
    baudrate = args.baud if args.baud else args.baudrate

    # Resolve port
    port = args.port
    if not port:
        print("Auto-detecting serial port...")
        port = find_serial_port()
        if not port:
            print("\n✗ No serial port found. Specify manually or use --list")
            sys.exit(1)
        print(f"✓ Found port: {port}")

    # Wire-speed info
    bytes_per_sec = baudrate / 10  # 8N1
    kb_pkt_ps = bytes_per_sec / 15  # 15-byte kb packet
    max_pkt_ps = bytes_per_sec / 107  # 107-byte max packet

    print(f"\n{'='*60}")
    print(f"  CH9329 Protocol Fuzzer — Wire-Speed Stress Test")
    print(f"{'='*60}")
    print(f"  Port:      {port}")
    print(f"  Baudrate:  {baudrate:,}")
    print(f"  Wire speed: {bytes_per_sec:,.0f} bytes/s ({baudrate/1024:.0f} KB/s theoretical)")
    print(f"  Max rate:  ~{kb_pkt_ps:,.0f} kb-pkts/s  or  ~{max_pkt_ps:,.0f} max-pkts/s")
    print(f"  Phase time: {args.time}s per phase")
    if args.phase:
        print(f"  Phase:     {args.phase} (single)")
    else:
        print(f"  Phases:    1-8 (all)")
    print(f"{'='*60}")

    # Open serial
    try:
        ser = serial.Serial(port, baudrate, timeout=0, write_timeout=1)
        print(f"\n✓ Opened {port} @ {baudrate}")
    except serial.SerialException as e:
        print(f"\n✗ Cannot open {port}: {e}")
        sys.exit(1)

    time.sleep(2)  # Wait for device to initialize
    ser.reset_output_buffer()
    ser.reset_input_buffer()

    # Build packet pools
    print("Building packet pools...")
    pools = build_packet_pools()
    total_pool = sum(len(v) for v in pools.values())
    print(f"✓ {total_pool:,} packets pre-built")

    # Start response receiver thread
    tracker = ResponseTracker()
    receiver = ResponseReceiver(ser, tracker)
    receiver.start()
    print("✓ Response receiver started")

    # Phase definitions
    phases = [
        (1, "Keyboard packets",       phase_keyboard),
        (2, "Mouse packets",          phase_mouse),
        (3, "Mixed KB + Mouse",       phase_mixed_kb_mouse),
        (4, "Oversized data_len",     phase_oversized),
        (5, "Boundary values",        phase_boundary),
        (6, "Invalid headers",        phase_bad_header),
        (7, "Mixed valid/malformed",  phase_mixed_valid_malformed),
        (8, "Sustained barrage",      phase_sustained),
    ]

    # Run phases
    results = []
    try:
        for num, name, func in phases:
            if args.phase and args.phase != num:
                continue
            ok = func(ser, tracker, args.time, pools, args.rate)
            results.append((num, name, ok))

    except KeyboardInterrupt:
        print("\n\n✗ Interrupted by user")
    except serial.SerialException as e:
        print(f"\n\n✗ Serial error: {e}")
        print("  Device may have disconnected or crashed!")
    finally:
        # Stop receiver
        receiver.stop()
        receiver.join(timeout=1)

        # Summary
        print(f"\n{'='*60}")
        print(f"  TEST SUMMARY")
        print(f"{'='*60}")
        for num, name, ok in results:
            status = "✓ PASS" if ok else "✗ FAIL"
            print(f"  Phase {num}: {status}  {name}")
        print(f"{'='*60}")

        if results and all(r[2] for r in results):
            print(f"\n  All {len(results)} phases completed.")
            print(f"  Check device for:")
            print(f"    - Crashes / resets / USB disconnects")
            print(f"    - LED indicator anomalies")
            print(f"    - Unresponsive behavior")

        ser.close()
        print(f"\n✓ Closed {port}")

if __name__ == "__main__":
    main()
