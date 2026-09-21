#!/usr/bin/env python3
"""
KVM-GO Unified Test & Configuration Tool
==========================================

Unified tool for testing and configuring KVM-GO / KeyMod devices.
Supports both BLE and USB (CDC ACM) communication.

Usage:
    python3 kvmgo_tool.py scan                          # Scan BLE devices
    python3 kvmgo_tool.py info --device KVM-GO          # Get device info via BLE
    python3 kvmgo_tool.py set "my-name" --device KVM-GO # Set BLE name
    python3 kvmgo_tool.py reset --device KVM-GO         # Reset BLE name to default
    python3 kvmgo_tool.py reboot --device KVM-GO        # Reboot device
    python3 kvmgo_tool.py diag --device KVM-GO          # Full BLE diagnostic
    python3 kvmgo_tool.py usb-info --port /dev/cu.xxx   # USB device info
    python3 kvmgo_tool.py usb-cmd 01 --port /dev/cu.xxx # Send USB command
    python3 kvmgo_tool.py test-all --device KVM-GO      # Run all tests

Commands:
    scan        - Scan for BLE devices
    info        - Get device info (CMD_GET_INFO 0x01)
    set NAME    - Set BLE device name (CMD_BLE_SET_NAME 0x23)
    reset       - Reset BLE name to factory default (CMD_SYSTEM_RESET 0x31 with 0x00)
    reboot      - Reboot device (CMD_SYSTEM_RESET 0x31)
    diag        - Full BLE diagnostic (services, characteristics)
    usb-info    - Get USB device info via CDC ACM serial
    usb-cmd HEX - Send raw CH9329 command via USB serial
    test-all    - Run comprehensive test suite

Protocol Notes (firmware v2):
    CMD 0x23 (BLE_SET_NAME):
        - Payload: 1-20 bytes name (UTF-8)
        - Empty payload or 0x00 returns ERR_PARAM (0xE5)
        - Requires reboot (0x31) to fully apply

    CMD 0x31 (SYSTEM_RESET):
        - No payload: normal system reboot
        - Payload 0x00: reset BLE name to default ("KVM-GO") + reboot
"""

import asyncio
import sys
import argparse
import time
from typing import Optional, Tuple

try:
    from bleak import BleakClient, BleakScanner
    BLE_AVAILABLE = True
except ImportError:
    BLE_AVAILABLE = False

try:
    import serial
    SERIAL_AVAILABLE = True
except ImportError:
    SERIAL_AVAILABLE = False

try:
    import usb.core
    import usb.util
    USB_AVAILABLE = True
except ImportError:
    USB_AVAILABLE = False


# ============================================================
# CH9329 Protocol Constants
# ============================================================
FRAME_HEAD1 = 0x57
FRAME_HEAD2 = 0xAB

CMD_GET_INFO        = 0x01
CMD_GET_PARA_CFG    = 0x08
CMD_BLE_SET_NAME    = 0x23
CMD_SYSTEM_RESET    = 0x31

STATUS_SUCCESS      = 0x00
STATUS_ERR_TIMEOUT  = 0xE1
STATUS_ERR_HEADER   = 0xE2
STATUS_ERR_CMD      = 0xE3
STATUS_ERR_CHECKSUM = 0xE4
STATUS_ERR_PARAM    = 0xE5
STATUS_ERR_FRAME    = 0xE6

# BLE Service UUIDs (Openterface KVM-GO / KeyMod)
BLE_USB_SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb"
BLE_USB_RX_UUID      = "0000fff1-0000-1000-8000-00805f9b34fb"  # Notify (device -> host)
BLE_USB_TX_UUID      = "0000fff2-0000-1000-8000-00805f9b34fb"  # Write  (host -> device)

# ANSI colors
GREEN  = "\033[32m"
RED    = "\033[31m"
YELLOW = "\033[33m"
CYAN   = "\033[36m"
RESET  = "\033[0m"
BOLD   = "\033[1m"


def ok(msg):    print(f"  {GREEN}✓{RESET} {msg}")
def fail(msg):  print(f"  {RED}✗{RESET} {msg}")
def warn(msg):  print(f"  {YELLOW}⚠{RESET} {msg}")
def info(msg):  print(f"  {CYAN}ℹ{RESET} {msg}")


# ============================================================
# Frame Building
# ============================================================
def calculate_checksum(data: bytes) -> int:
    """CH9329 checksum: sum of all bytes, masked to 8 bits."""
    return sum(data) & 0xFF


def build_frame(addr: int, cmd: int, payload: bytes = b'') -> bytes:
    """Build a CH9329 command frame."""
    data_len = len(payload)
    frame = bytearray([FRAME_HEAD1, FRAME_HEAD2, addr, cmd, data_len])
    frame.extend(payload)
    frame.append(calculate_checksum(bytes(frame)))
    return bytes(frame)


def build_get_info(addr: int = 0x00) -> bytes:
    return build_frame(addr, CMD_GET_INFO)


def build_set_name(name: str, addr: int = 0x00) -> bytes:
    """Build CMD_BLE_SET_NAME frame. Name must be 1-20 bytes."""
    name_bytes = name.encode('utf-8')[:20]
    if len(name_bytes) == 0:
        raise ValueError("Name must not be empty")
    return build_frame(addr, CMD_BLE_SET_NAME, name_bytes)


def build_system_reset(addr: int = 0x00, reset_name: bool = False) -> bytes:
    """Build CMD_SYSTEM_RESET frame.

    Args:
        addr: Device address
        reset_name: If True, also reset BLE name to factory default before reboot
    """
    payload = b'\x00' if reset_name else b''
    return build_frame(addr, CMD_SYSTEM_RESET, payload)


def build_get_para_cfg(addr: int = 0x00) -> bytes:
    return build_frame(addr, CMD_GET_PARA_CFG)


# ============================================================
# Frame Parsing
# ============================================================
def parse_response(response: bytes) -> dict:
    """Parse a CH9329 response frame."""
    if len(response) < 6:
        return {'error': 'Response too short'}
    if response[0] != FRAME_HEAD1 or response[1] != FRAME_HEAD2:
        return {'error': 'Invalid frame header'}

    addr = response[2]
    cmd_code = response[3]
    data_len = response[4]

    if len(response) < 5 + data_len + 1:
        return {'error': f'Incomplete response (expected {5 + data_len + 1} bytes, got {len(response)})'}

    data = response[5:5 + data_len]
    checksum = response[5 + data_len]
    expected_checksum = calculate_checksum(response[0:5 + data_len])

    if checksum != expected_checksum:
        return {'error': f'Checksum mismatch: got {checksum:02X}, expected {expected_checksum:02X}'}

    return {
        'addr': addr,
        'cmd_code': cmd_code,
        'data_len': data_len,
        'data': data,
        'checksum_ok': True
    }


def decode_status(status_byte: int) -> str:
    """Decode status byte to human-readable string."""
    status_map = {
        STATUS_SUCCESS:      'SUCCESS',
        STATUS_ERR_TIMEOUT:  'ERR_TIMEOUT',
        STATUS_ERR_HEADER:   'ERR_HEADER',
        STATUS_ERR_CMD:      'ERR_CMD (unsupported)',
        STATUS_ERR_CHECKSUM: 'ERR_CHECKSUM',
        STATUS_ERR_PARAM:    'ERR_PARAM (invalid parameter)',
        STATUS_ERR_FRAME:    'ERR_FRAME (operation failed)',
    }
    return status_map.get(status_byte, f'UNKNOWN (0x{status_byte:02X})')


def decode_get_info(data: bytes) -> dict:
    """Decode CMD_GET_INFO response."""
    if len(data) < 8:
        return {'error': f'Response too short ({len(data)} bytes, need 8)'}

    hw_map = {1: 'KVM-GO', 2: 'MiniKVM', 3: 'KeyMod'}
    return {
        'hw_version': data[0],
        'hw_name': hw_map.get(data[0], f'Unknown ({data[0]})'),
        'usb_connected': bool(data[1] & 0x01),
        'led_status': data[2] & 0x07,
        'reserved': data[3:8].hex(' '),
    }


# ============================================================
# BLE Communication
# ============================================================
async def ble_scan(timeout: float = 5.0, filter_name: str = None):
    """Scan for BLE devices."""
    if not BLE_AVAILABLE:
        fail("bleak not installed. Run: pip3 install bleak")
        return []

    print(f"\n{BOLD}Scanning for BLE devices{RESET} (timeout: {timeout}s)...\n")
    devices = await BleakScanner.discover(timeout=timeout)

    # Sort: named first, then unnamed
    named = sorted([d for d in devices if d.name], key=lambda d: d.name.lower())
    unnamed = [d for d in devices if not d.name]
    all_devices = named + unnamed

    # Apply filter
    if filter_name:
        filtered = [d for d in all_devices if filter_name.lower() in (d.name or '').lower()]
    else:
        # Default: show KVM/KeyMod/Openterface devices first
        known = [d for d in all_devices if d.name and any(
            kw in d.name.lower() for kw in ['kvm', 'keymod', 'openterface']
        )]
        others = [d for d in all_devices if d not in known]
        filtered = known + others

    if not filtered:
        fail("No matching BLE devices found")
        return []

    print(f"Found {len(filtered)} device(s):\n")
    for i, d in enumerate(filtered, 1):
        name = d.name if d.name else "(Unknown)"
        marker = " ★" if d.name and any(
            kw in d.name.lower() for kw in ['kvm', 'keymod', 'openterface']
        ) else ""
        print(f"  [{i:2d}] {name:<30s}  {d.address}{marker}")

    print()
    return filtered


async def ble_connect_and_send(device_identifier: str, command: bytes,
                                wait_response: bool = True, timeout: float = 10.0,
                                response_wait: float = 2.0) -> Tuple[Optional[bytes], Optional[str]]:
    """Connect to a BLE device and send a command.

    Args:
        device_identifier: Device name or address
        command: Raw command bytes to send
        wait_response: Whether to wait for a response
        timeout: Connection timeout
        response_wait: How long to wait for response after sending

    Returns:
        (response_bytes, error_string) - one will be None
    """
    if not BLE_AVAILABLE:
        return None, "bleak not installed"

    # Find device
    devices = await BleakScanner.discover(timeout=3.0)
    target = None

    for d in devices:
        if d.address.lower() == device_identifier.lower():
            target = d
            break
        if d.name and device_identifier.lower() in d.name.lower():
            target = d
            break

    if not target:
        return None, f"Device '{device_identifier}' not found"

    print(f"  Target: {target.name} ({target.address})")
    print(f"  Connecting...")

    try:
        async with BleakClient(target, timeout=timeout) as client:
            if not client.is_connected:
                return None, "Connection failed"

            print(f"  ✓ Connected")

            # Find characteristics
            tx_char = None
            rx_char = None

            for service in client.services:
                if BLE_USB_SERVICE_UUID.lower() in service.uuid.lower():
                    for char in service.characteristics:
                        if BLE_USB_TX_UUID.lower() in char.uuid.lower():
                            tx_char = char
                        elif BLE_USB_RX_UUID.lower() in char.uuid.lower():
                            rx_char = char

            if not tx_char:
                return None, "TX characteristic (FFF2) not found"

            print(f"  ✓ Found TX: {tx_char.uuid}")
            if rx_char:
                print(f"  ✓ Found RX: {rx_char.uuid}")

            # Send command
            print(f"  Sending: {command.hex(' ')}")

            response_data = bytearray()

            if rx_char and wait_response:
                def notification_handler(sender, data):
                    response_data.extend(data)

                await client.start_notify(rx_char, notification_handler)
                await client.write_gatt_char(tx_char, command, response=False)
                await asyncio.sleep(response_wait)
                await client.stop_notify(rx_char)

                if response_data:
                    return bytes(response_data), None
                return None, "No response received"
            else:
                await client.write_gatt_char(tx_char, command, response=False)
                if wait_response:
                    await asyncio.sleep(response_wait)
                return None, None  # Fire-and-forget

    except asyncio.TimeoutError:
        return None, "Connection timeout"
    except Exception as e:
        return None, str(e) if str(e) else "Connection error"


# ============================================================
# USB Serial Communication
# ============================================================
def usb_serial_send(port: str, command: bytes, baud: int = 9600,
                     timeout: float = 2.0) -> Tuple[Optional[bytes], Optional[str]]:
    """Send command via USB CDC ACM serial port."""
    if not SERIAL_AVAILABLE:
        return None, "pyserial not installed"

    try:
        ser = serial.Serial(port, baud, timeout=timeout)
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        # Clear pending data
        try:
            pending = ser.read(100)
            if pending:
                warn(f"Cleared {len(pending)} pending bytes")
        except:
            pass

        ser.write(command)
        ser.flush()

        # Read response
        t0 = time.time()
        buf = bytearray()
        while time.time() - t0 < timeout:
            if ser.in_waiting:
                chunk = ser.read(ser.in_waiting)
                buf.extend(chunk)
                if len(buf) >= 6 and len(buf) >= 5 + buf[4] + 1:
                    break
            time.sleep(0.05)

        ser.close()

        if buf:
            return bytes(buf), None
        return None, "No response received"

    except serial.SerialException as e:
        return None, f"Serial error: {e}"
    except Exception as e:
        return None, str(e)


def usb_direct_send(vendor_id: int = 0x1a86, product_id: int = 0xfe0c,
                     command: bytes = None, timeout: float = 2.0) -> Tuple[Optional[bytes], Optional[str]]:
    """Send command via direct USB (pyusb), bypassing serial driver."""
    if not USB_AVAILABLE:
        return None, "pyusb not installed"
    if command is None:
        return None, "No command provided"

    try:
        dev = usb.core.find(idVendor=vendor_id, idProduct=product_id)
        if dev is None:
            return None, f"USB device {vendor_id:04x}:{product_id:04x} not found"

        try:
            if dev.is_kernel_driver_active(1):
                dev.detach_kernel_driver(1)
        except:
            pass

        usb.util.claim_interface(dev, 1)

        # Clear pending
        try:
            while True:
                dev.read(0x83, 64, timeout=30)
        except:
            pass

        dev.write(0x05, command, timeout=1000)
        time.sleep(0.3)

        data = dev.read(0x83, 64, timeout=int(timeout * 1000))
        usb.util.release_interface(dev, 1)

        return bytes(data), None

    except usb.core.USBError as e:
        return None, f"USB error: {e}"
    except Exception as e:
        return None, str(e)


# ============================================================
# Command Handlers
# ============================================================
async def cmd_scan(args):
    await ble_scan(timeout=args.timeout, filter_name=args.filter)


async def cmd_info(args):
    print(f"\n{BOLD}Device Info{RESET}")
    print(f"  Querying: {args.device}\n")

    cmd = build_get_info()
    response, error = await ble_connect_and_send(args.device, cmd)

    if error:
        fail(f"Error: {error}")
        return

    parsed = parse_response(response)
    if 'error' in parsed:
        fail(f"Parse error: {parsed['error']}")
        return

    info_data = decode_get_info(parsed['data'])
    if 'error' in info_data:
        fail(f"Decode error: {info_data['error']}")
        return

    print(f"\n{BOLD}Device Information:{RESET}")
    print(f"  Hardware:     {info_data['hw_name']} (v{info_data['hw_version']})")
    print(f"  USB Target:   {'Connected' if info_data['usb_connected'] else 'Disconnected'}")
    print(f"  LED Status:   0x{info_data['led_status']:02X}")
    print(f"  Reserved:     {info_data['reserved']}")


async def cmd_set_name(args):
    name = args.name
    print(f"\n{BOLD}Set BLE Name{RESET}")
    print(f"  Device:  {args.device}")
    print(f"  New name: \"{name}\" ({len(name)} chars)\n")

    if len(name) == 0:
        fail("Name must not be empty")
        return
    if len(name.encode('utf-8')) > 20:
        fail("Name too long (max 20 bytes UTF-8)")
        return

    cmd = build_set_name(name)
    response, error = await ble_connect_and_send(args.device, cmd)

    if error:
        fail(f"Error: {error}")
        return

    parsed = parse_response(response)
    if 'error' in parsed:
        fail(f"Parse error: {parsed['error']}")
        return

    expected_cmd = CMD_BLE_SET_NAME | 0x80
    if parsed['cmd_code'] != expected_cmd:
        fail(f"Unexpected response command: 0x{parsed['cmd_code']:02X}")
        return

    status = parsed['data'][0] if parsed['data'] else 0xFF
    if status == STATUS_SUCCESS:
        ok(f"BLE name set to \"{name}\"")
        print()
        warn("Remember: name change requires a reboot to fully apply.")
        warn("Run: python3 kvmgo_tool.py reboot --device <new-name-or-address>")
    else:
        fail(f"Failed: {decode_status(status)}")


async def cmd_reset(args):
    print(f"\n{BOLD}Reset BLE Name to Factory Default{RESET}")
    print(f"  Device: {args.device}")
    print(f"  This will reset the name to \"KVM-GO\" and reboot the device.\n")

    cmd = build_system_reset(reset_name=True)
    response, error = await ble_connect_and_send(args.device, cmd, response_wait=1.0)

    if error and "not found" not in error and "timeout" not in error.lower():
        fail(f"Error: {error}")
        return

    ok("Reset command sent. Device is rebooting...")
    info("BLE name will be \"KVM-GO\" after reboot.")


async def cmd_reboot(args):
    print(f"\n{BOLD}Reboot Device{RESET}")
    print(f"  Device: {args.device}\n")

    cmd = build_system_reset(reset_name=False)
    response, error = await ble_connect_and_send(args.device, cmd, response_wait=1.0)

    if error and "not found" not in error and "timeout" not in error.lower():
        fail(f"Error: {error}")
        return

    ok("Reboot command sent. Device is restarting...")


async def cmd_diag(args):
    if not BLE_AVAILABLE:
        fail("bleak not installed")
        return

    print(f"\n{BOLD}BLE Diagnostic{RESET}")
    print(f"  Device: {args.device}\n")

    # Scan
    devices = await BleakScanner.discover(timeout=3.0)
    target = None
    for d in devices:
        if d.address.lower() == args.device.lower():
            target = d
            break
        if d.name and args.device.lower() in d.name.lower():
            target = d
            break

    if not target:
        fail(f"Device '{args.device}' not found")
        return

    print(f"  Target: {target.name} ({target.address})")
    print(f"  RSSI: {target.rssi if hasattr(target, 'rssi') else 'N/A'} dBm")
    print(f"  Connecting...\n")

    try:
        async with BleakClient(target, timeout=15.0) as client:
            if not client.is_connected:
                fail("Connection failed")
                return

            ok("Connected")

            # Get device name
            try:
                dev_name = await client.get_device_name()
                info(f"Device name (GAP): {dev_name}")
            except:
                pass

            print(f"\n{BOLD}Services & Characteristics:{RESET}\n")

            for service in client.services:
                print(f"  📦 Service: {service.uuid}")
                if service.description:
                    print(f"              {service.description}")

                for char in service.characteristics:
                    props = ", ".join(char.properties)
                    print(f"      ├─ Char: {char.uuid}")
                    print(f"      │   Properties: [{props}]")

                    if "read" in char.properties:
                        try:
                            value = await client.read_gatt_char(char)
                            print(f"      │   Value: {value.hex(' ')}")
                        except Exception as e:
                            print(f"      │   Value: [read failed: {e}]")

                    for desc in char.descriptors:
                        try:
                            value = await client.read_gatt_char(desc)
                            print(f"      │   Desc: {desc.uuid} = {value.hex(' ')}")
                        except:
                            print(f"      │   Desc: {desc.uuid}")

                    print(f"      │")

            # Test write to FFF2
            print(f"\n{BOLD}Communication Test:{RESET}")
            tx_char = None
            rx_char = None
            for service in client.services:
                if BLE_USB_SERVICE_UUID.lower() in service.uuid.lower():
                    for char in service.characteristics:
                        if BLE_USB_TX_UUID.lower() in char.uuid.lower():
                            tx_char = char
                        elif BLE_USB_RX_UUID.lower() in char.uuid.lower():
                            rx_char = char

            if tx_char:
                ok(f"TX characteristic found: {tx_char.uuid}")
                test_cmd = build_get_info()
                response_data = bytearray()

                if rx_char:
                    def handler(sender, data):
                        response_data.extend(data)
                    await client.start_notify(rx_char, handler)
                    await client.write_gatt_char(tx_char, test_cmd, response=False)
                    await asyncio.sleep(1.5)
                    await client.stop_notify(rx_char)

                    if response_data:
                        ok(f"Response received: {bytes(response_data).hex(' ')}")
                        parsed = parse_response(bytes(response_data))
                        if 'error' not in parsed:
                            info_data = decode_get_info(parsed['data'])
                            if 'error' not in info_data:
                                ok(f"Device: {info_data['hw_name']}, USB: {'Connected' if info_data['usb_connected'] else 'Disconnected'}")
                    else:
                        warn("No response from GET_INFO command")
            else:
                fail("TX characteristic (FFF2) not found")

            print(f"\n{BOLD}Diagnostic complete.{RESET}\n")

    except asyncio.TimeoutError:
        fail("Connection timeout")
    except Exception as e:
        fail(f"Error: {e}")


def cmd_usb_info(args):
    if not SERIAL_AVAILABLE:
        fail("pyserial not installed")
        return

    port = args.port
    baud = args.baud

    print(f"\n{BOLD}USB Device Info{RESET}")
    print(f"  Port: {port}")
    print(f"  Baud: {baud}\n")

    response, error = usb_serial_send(port, build_get_info(), baud=baud)

    if error:
        fail(f"Error: {error}")
        return

    parsed = parse_response(response)
    if 'error' in parsed:
        fail(f"Parse error: {parsed['error']}")
        return

    info_data = decode_get_info(parsed['data'])
    if 'error' in info_data:
        fail(f"Decode error: {info_data['error']}")
        return

    print(f"{BOLD}Device Information:{RESET}")
    print(f"  Hardware:     {info_data['hw_name']} (v{info_data['hw_version']})")
    print(f"  USB Target:   {'Connected' if info_data['usb_connected'] else 'Disconnected'}")
    print(f"  LED Status:   0x{info_data['led_status']:02X}")


def cmd_usb_cmd(args):
    if not SERIAL_AVAILABLE:
        fail("pyserial not installed")
        return

    port = args.port
    baud = args.baud

    # Parse hex string
    try:
        hex_str = args.hex_data.replace(' ', '').replace('-', '')
        payload = bytes.fromhex(hex_str)
    except ValueError:
        fail(f"Invalid hex string: {args.hex_data}")
        return

    print(f"\n{BOLD}USB Raw Command{RESET}")
    print(f"  Port: {port}")
    print(f"  Data: {payload.hex(' ')}\n")

    response, error = usb_serial_send(port, payload, baud=baud)

    if error:
        fail(f"Error: {error}")
        return

    print(f"  Response: {response.hex(' ')}")
    parsed = parse_response(response)
    if 'error' in parsed:
        fail(f"Parse error: {parsed['error']}")
    else:
        ok(f"Valid CH9329 frame, cmd=0x{parsed['cmd_code']:02X}, data={parsed['data'].hex(' ')}")


async def cmd_test_all(args):
    print(f"\n{BOLD}{'='*60}{RESET}")
    print(f"{BOLD}KVM-GO Comprehensive Test Suite{RESET}")
    print(f"{BOLD}{'='*60}{RESET}")

    results = {}

    # Test 1: BLE Scan
    print(f"\n{BOLD}Test 1: BLE Scan{RESET}")
    devices = await ble_scan(timeout=3.0, filter_name=args.device)
    results["BLE Scan"] = len(devices) > 0
    if devices:
        ok(f"Found {len(devices)} matching device(s)")
    else:
        fail("No matching devices found")

    if not devices:
        print(f"\n{RED}Cannot proceed without device.{RESET}")
        return

    target = devices[0]

    # Test 2: BLE Connection
    print(f"\n{BOLD}Test 2: BLE Connection{RESET}")
    cmd = build_get_info()
    response, error = await ble_connect_and_send(target.address, cmd)

    if error:
        fail(f"Connection failed: {error}")
        results["BLE Connection"] = False
    else:
        ok("Connected and received response")
        results["BLE Connection"] = True

        # Test 3: Response Parsing
        print(f"\n{BOLD}Test 3: CH9329 Response Parsing{RESET}")
        parsed = parse_response(response)
        if 'error' in parsed:
            fail(f"Parse error: {parsed['error']}")
            results["Response Parsing"] = False
        else:
            ok(f"Frame header: {FRAME_HEAD1:02X} {FRAME_HEAD2:02X}")
            ok(f"Command: 0x{parsed['cmd_code']:02X}")
            ok(f"Data length: {parsed['data_len']}")
            ok(f"Checksum: valid")
            results["Response Parsing"] = True

            # Test 4: Device Info Decode
            print(f"\n{BOLD}Test 4: Device Info Decode{RESET}")
            info_data = decode_get_info(parsed['data'])
            if 'error' in info_data:
                fail(f"Decode error: {info_data['error']}")
                results["Info Decode"] = False
            else:
                ok(f"Hardware: {info_data['hw_name']}")
                ok(f"USB: {'Connected' if info_data['usb_connected'] else 'Disconnected'}")
                results["Info Decode"] = True

    # Test 5: BLE Set Name (dry run - just build command)
    print(f"\n{BOLD}Test 5: Command Building{RESET}")
    try:
        test_name = "test-1234"
        set_cmd = build_set_name(test_name)
        expected_checksum = calculate_checksum(set_cmd[:-1])
        if set_cmd[-1] == expected_checksum:
            ok(f"SET_NAME frame valid: {set_cmd.hex(' ')}")
            results["Command Building"] = True
        else:
            fail("SET_NAME checksum mismatch")
            results["Command Building"] = False
    except Exception as e:
        fail(f"Command building error: {e}")
        results["Command Building"] = False

    # Test 6: System Reset command building
    print(f"\n{BOLD}Test 6: System Reset Command{RESET}")
    try:
        reset_cmd = build_system_reset(reset_name=False)
        reset_with_name = build_system_reset(reset_name=True)
        ok(f"RESET (normal):      {reset_cmd.hex(' ')}")
        ok(f"RESET (reset name):  {reset_with_name.hex(' ')}")
        results["Reset Command"] = True
    except Exception as e:
        fail(f"Reset command error: {e}")
        results["Reset Command"] = False

    # Summary
    print(f"\n{BOLD}{'='*60}{RESET}")
    print(f"{BOLD}Test Summary{RESET}")
    print(f"{BOLD}{'='*60}{RESET}\n")

    all_pass = True
    for name, passed in results.items():
        if passed:
            ok(f"{name}")
        else:
            fail(f"{name}")
            all_pass = False

    print()
    if all_pass:
        print(f"  {GREEN}{BOLD}ALL TESTS PASSED{RESET}")
    else:
        print(f"  {RED}{BOLD}SOME TESTS FAILED{RESET}")
    print()


# ============================================================
# Main
# ============================================================
def main():
    parser = argparse.ArgumentParser(
        description='KVM-GO Unified Test & Configuration Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s scan                              Scan for BLE devices
  %(prog)s scan --filter KVM                 Scan and filter by name
  %(prog)s info --device KVM-GO              Get device info via BLE
  %(prog)s set "my-name" --device KVM-GO     Set BLE name
  %(prog)s reset --device KVM-GO             Reset name to default + reboot
  %(prog)s reboot --device kvmgo-test        Reboot device (keep name)
  %(prog)s diag --device KVM-GO              Full BLE diagnostic
  %(prog)s usb-info --port /dev/cu.usbmodem101
  %(prog)s test-all --device KVM-GO          Run all tests

Protocol (firmware v2):
  CMD 0x23 (BLE_SET_NAME): 1-20 bytes name, empty/0x00 = error
  CMD 0x31 (SYSTEM_RESET):  no payload = reboot, 0x00 = reset name + reboot
        """
    )

    subparsers = parser.add_subparsers(dest='command', help='Command to execute')

    # scan
    p_scan = subparsers.add_parser('scan', help='Scan for BLE devices')
    p_scan.add_argument('--timeout', type=float, default=5.0, help='Scan timeout (seconds)')
    p_scan.add_argument('--filter', default=None, help='Filter by device name substring')

    # info
    p_info = subparsers.add_parser('info', help='Get device info via BLE')
    p_info.add_argument('--device', required=True, help='Device name or address')

    # set
    p_set = subparsers.add_parser('set', help='Set BLE device name')
    p_set.add_argument('name', help='New BLE name (1-20 characters)')
    p_set.add_argument('--device', required=True, help='Target device name or address')

    # reset
    p_reset = subparsers.add_parser('reset', help='Reset BLE name to default + reboot')
    p_reset.add_argument('--device', required=True, help='Target device name or address')

    # reboot
    p_reboot = subparsers.add_parser('reboot', help='Reboot device (keep current name)')
    p_reboot.add_argument('--device', required=True, help='Target device name or address')

    # diag
    p_diag = subparsers.add_parser('diag', help='Full BLE diagnostic')
    p_diag.add_argument('--device', required=True, help='Target device name or address')

    # usb-info
    p_usb_info = subparsers.add_parser('usb-info', help='Get device info via USB serial')
    p_usb_info.add_argument('--port', required=True, help='Serial port (e.g. /dev/cu.usbmodem101)')
    p_usb_info.add_argument('--baud', type=int, default=9600, help='Baud rate (default: 9600)')

    # usb-cmd
    p_usb_cmd = subparsers.add_parser('usb-cmd', help='Send raw CH9329 command via USB')
    p_usb_cmd.add_argument('hex_data', help='Hex-encoded command (e.g. "57AB0001000003")')
    p_usb_cmd.add_argument('--port', required=True, help='Serial port')
    p_usb_cmd.add_argument('--baud', type=int, default=9600, help='Baud rate')

    # test-all
    p_test = subparsers.add_parser('test-all', help='Run comprehensive test suite')
    p_test.add_argument('--device', required=True, help='Target device name or address')

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return

    # Dispatch
    if args.command == 'scan':
        asyncio.run(cmd_scan(args))
    elif args.command == 'info':
        asyncio.run(cmd_info(args))
    elif args.command == 'set':
        asyncio.run(cmd_set_name(args))
    elif args.command == 'reset':
        asyncio.run(cmd_reset(args))
    elif args.command == 'reboot':
        asyncio.run(cmd_reboot(args))
    elif args.command == 'diag':
        asyncio.run(cmd_diag(args))
    elif args.command == 'usb-info':
        cmd_usb_info(args)
    elif args.command == 'usb-cmd':
        cmd_usb_cmd(args)
    elif args.command == 'test-all':
        asyncio.run(cmd_test_all(args))


if __name__ == '__main__':
    main()
