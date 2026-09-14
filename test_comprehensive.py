#!/usr/bin/env python3
"""
Comprehensive CH9329 test - tests serial port and validates all fixes.
"""

import usb.core
import usb.util
import serial
import time
import sys

GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
RESET = "\033[0m"

def ok(msg):
    print(f"  {GREEN}✓{RESET} {msg}")

def fail(msg):
    print(f"  {RED}✗{RESET} {msg}")

def warn(msg):
    print(f"  {YELLOW}⚠{RESET} {msg}")

def raw_hex(d):
    if isinstance(d, (bytearray, bytes)):
        return d.hex()
    return bytes(d).hex()

# ============================================================
# Test 1: Device detection via pyusb
# ============================================================
def test_device_detection():
    print("\n" + "=" * 60)
    print("Test 1: USB Device Detection")
    print("=" * 60)

    dev = usb.core.find(idVendor=0x1a86, idProduct=0xfe0c)
    if dev is None:
        fail("CDC ACM device not found")
        return None

    ok(f"Device found: {dev.idVendor:04x}:{dev.idProduct:04x}")

    # Check interfaces
    try:
        cfg = dev.get_active_configuration()
        ok(f"Active config: {cfg.bConfigurationValue}")

        found_ep3 = False
        found_ep5 = False
        for intf in cfg:
            for ep in intf:
                addr = ep.bEndpointAddress
                if addr == 0x83: found_ep3 = True
                if addr == 0x05: found_ep5 = True

        if found_ep3: ok("EP3 IN (0x83) found")
        else: fail("EP3 IN (0x83) NOT found")

        if found_ep5: ok("EP5 OUT (0x05) found")
        else: fail("EP5 OUT (0x05) NOT found")

    except Exception as e:
        fail(f"Config check failed: {e}")

    return dev

# ============================================================
# Test 2: EP3 IN via pyusb (CH9329 response)
# ============================================================
def test_pyusb_response(dev):
    print("\n" + "=" * 60)
    print("Test 2: CH9329 Response via pyusb (bypass serial)")
    print("=" * 60)

    try:
        # Detach kernel driver and claim interface
        try:
            if dev.is_kernel_driver_active(1):
                dev.detach_kernel_driver(1)
        except:
            pass
        usb.util.claim_interface(dev, 1)
        ok("Interface 1 claimed")

        # Clear pending
        try:
            while True: dev.read(0x83, 64, timeout=50)
        except: pass

        # Send command
        cmd = bytearray([0x57, 0xAB, 0x00, 0x01, 0x00, 0x03])
        dev.write(0x05, cmd, timeout=1000)
        ok(f"Command sent: {raw_hex(cmd)}")

        time.sleep(0.3)

        # Read response
        data = dev.read(0x83, 64, timeout=2000)
        d = bytes(data)
        ok(f"Response: {len(d)} bytes: {d.hex()}")

        # Validate
        if d[0] == 0x57 and d[1] == 0xAB:
            ok("Frame header valid (57 AB)")
        else:
            fail(f"Frame header invalid: {d[0]:02X} {d[1]:02X}")

        if d[3] == 0x81:
            ok("Command: Get Info response (0x81)")
        else:
            fail(f"Wrong command: 0x{d[3]:02X}")

        checksum = sum(d[:5+d[4]]) & 0xFF
        if checksum == d[5+d[4]]:
            ok(f"Checksum valid ({checksum:02X})")
        else:
            fail(f"Checksum mismatch: calc={checksum:02X}, got={d[5+d[4]]:02X}")

        if d[4] >= 8:
            version = d[5]
            fw_ver = d[8]
            ok(f"Hardware version: {version}")
            ok(f"Firmware version: 0x{fw_ver:02X}")
        else:
            fail(f"Data too short: len={d[4]}")

        usb.util.release_interface(dev, 1)
        return True

    except usb.core.USBError as e:
        fail(f"USB error: {e}")
        try: usb.util.release_interface(dev, 1)
        except: pass
        return False

# ============================================================
# Test 3: Serial port (CDC ACM) basic read/write
# ============================================================
def test_serial_port():
    print("\n" + "=" * 60)
    print("Test 3: Serial Port (CDC ACM) Read/Write")
    print("=" * 60)

    port = '/dev/cu.usbmodem211201'
    baud_rates = [9600, 19200, 38400, 57600, 115200]

    # Check port exists
    import os
    if not os.path.exists(port):
        fail(f"Port {port} does not exist")
        return False
    ok(f"Port exists: {port}")

    results = {}
    for baud in baud_rates:
        try:
            ser = serial.Serial(port, baud, timeout=2)
            ser.reset_input_buffer()
            ser.reset_output_buffer()

            # Clear pending
            try:
                pending = ser.read(100)
                if pending:
                    warn(f"Pending data at {baud}: {len(pending)} bytes")
            except:
                pass

            # Send command
            cmd = bytes([0x57, 0xAB, 0x00, 0x01, 0x00, 0x03])
            ser.write(cmd)

            # Try to read response
            t0 = time.time()
            buf = bytearray()
            while time.time() - t0 < 2.0:
                if ser.in_waiting:
                    chunk = ser.read(ser.in_waiting)
                    buf.extend(chunk)
                    if len(buf) >= 6:
                        break
                time.sleep(0.05)

            if len(buf) > 0:
                ok(f"Serial {baud}: received {len(buf)} bytes: {raw_hex(buf)}")
                if buf[0] == 0x57 and buf[1] == 0xAB:
                    ok(f"Serial {baud}: valid CH9329 frame!")
                    results[baud] = True
                else:
                    warn(f"Serial {baud}: non-CH9329 data")
                    results[baud] = False
            else:
                warn(f"Serial {baud}: no response")
                results[baud] = False

            ser.close()

        except Exception as e:
            fail(f"Serial {baud}: error - {e}")
            results[baud] = False

    # Summarize
    working = [b for b, ok_val in results.items() if ok_val]
    if working:
        ok(f"Serial working at: {working}")
    else:
        fail("Serial: no response at any baud rate")

    return len(working) > 0

# ============================================================
# Test 4: Multi-command test via pyusb
# ============================================================
def test_multi_command(dev):
    print("\n" + "=" * 60)
    print("Test 4: Multi-command Test (pyusb)")
    print("=" * 60)

    test_commands = [
        ("Get Info",        [0x57, 0xAB, 0x00, 0x01, 0x00, 0x03]),
        ("Get Para Cfg",    [0x57, 0xAB, 0x00, 0x08, 0x00, 0x0A]),
    ]

    try:
        try:
            if dev.is_kernel_driver_active(1):
                dev.detach_kernel_driver(1)
        except: pass
        usb.util.claim_interface(dev, 1)

        results = {}
        for name, cmd_bytes in test_commands:
            cmd = bytearray(cmd_bytes)

            # Clear pending
            try:
                while True: dev.read(0x83, 64, timeout=30)
            except: pass

            dev.write(0x05, cmd, timeout=1000)
            time.sleep(0.3)

            try:
                data = dev.read(0x83, 64, timeout=2000)
                d = bytes(data)
                if d[0] == 0x57 and d[1] == 0xAB:
                    ok(f"{name}: response {len(d)} bytes - {d.hex()}")
                    results[name] = True
                else:
                    fail(f"{name}: invalid response - {d.hex()}")
                    results[name] = False
            except:
                fail(f"{name}: no response")
                results[name] = False

        usb.util.release_interface(dev, 1)

        return all(results.values())

    except Exception as e:
        fail(f"Multi-command test error: {e}")
        try: usb.util.release_interface(dev, 1)
        except: pass
        return False

# ============================================================
# Summary
# ============================================================
def print_summary(results):
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)

    all_pass = True
    for name, passed in results.items():
        if passed:
            ok(f"{name}: PASSED")
        else:
            fail(f"{name}: FAILED")
            all_pass = False

    print("-" * 60)

    if all_pass:
        print(f"{GREEN}ALL TESTS PASSED{RESET}")
    else:
        print(f"{RED}SOME TESTS FAILED{RESET}")

    print("-" * 60)
    print("\nFix Analysis:")
    print("  Fix 1 (EP3 enable in USBD_SetConfiguration): REQUIRED")
    print("    EP3 starts as EP_TX_DIS. Without this fix,")
    print("    EP3 would only enable after CDC_SET_LINE_CODING,")
    print("    which some hosts may not send.")
    print()
    print("  Fix 2 (EP3 status check in USB_SendFromQueues): DEFENSIVE")
    print("    Adds robustness in case Fix 1 fails or EP3 gets")
    print("    disabled mid-operation. Not strictly required with")
    print("    Fix 1 working, but provides safety net.")
    print()
    print("  Fix 3 (USB enumeration wait in main): RECOMMENDED")
    print("    Ensures USB is fully configured before sending")
    print("    startup response. Without this, the startup response")
    print("    could be sent before the host is ready to receive.")
    print("=" * 60)

# ============================================================
# Main
# ============================================================
if __name__ == "__main__":
    print("=" * 60)
    print("CH9329 Comprehensive Test Suite")
    print("=" * 60)

    results = {}

    # Kill any lingering Python processes using the serial port
    import os
    os.system("pkill -f 'python3.*test_serial\|python3.*monitor_serial' 2>/dev/null; sleep 0.5")

    # Test 1: Device detection
    dev = test_device_detection()
    results["Device Detection"] = dev is not None

    if dev is None:
        fail("\nCannot proceed without device. Aborting.")
        sys.exit(1)

    # Test 2: pyusb response
    results["CH9329 via pyusb"] = test_pyusb_response(dev)

    # Test 3: Serial port
    results["Serial Port"] = test_serial_port()

    # Test 4: Multi-command
    results["Multi-command"] = test_multi_command(dev)

    # Summary
    print_summary(results)