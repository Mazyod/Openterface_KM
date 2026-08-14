"""Shared test fixtures for Openterface KM regression tests."""
import asyncio
import glob
import os
import time

import pytest
import serial
import argparse

# BLE names advertised by Openterface firmware (per board version)
KNOWN_BLE_NAMES = ("KeyMod", "KVM-GO", "Mini-KVM")


def _detect_control_port(baud=115200):
    """Find the CDC port that answers CH9329 GET_INFO (0x01)."""
    frame = bytes([0x57, 0xAB, 0x00, 0x01, 0x00])
    frame += bytes([sum(frame) & 0xFF])
    for port in sorted(glob.glob("/dev/cu.usbmodem*")):
        try:
            ser = serial.Serial(port, baud, timeout=1)
        except (OSError, serial.SerialException):
            continue
        try:
            ser.reset_input_buffer()
            ser.write(frame)
            time.sleep(0.3)
            resp = ser.read(64)
            if len(resp) >= 4 and resp[0] == 0x57 and resp[1] == 0xAB and resp[3] == 0x81:
                return port
        finally:
            ser.close()
    return None


@pytest.fixture(scope="session")
def serial_port():
    """Open serial port to device for the test session."""
    baud = 115200
    port = os.environ.get("OPENTERFACE_PORT") or _detect_control_port(baud)
    if port is None:
        pytest.skip("No Openterface control serial port detected")
    ser = serial.Serial(port, baud, timeout=3)
    yield ser
    ser.close()


_ble_cache = None


def _detect_ble_device(timeout=10):
    """Scan for an Openterface BLE device advertising under a known name.

    Returns a (name, address) tuple, or (None, None) when nothing is found
    or bleak is unavailable.
    """
    global _ble_cache
    if _ble_cache is not None:
        return _ble_cache

    result = (None, None)
    try:
        from bleak import BleakScanner
    except ImportError:
        return result

    async def _scan():
        devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
        # Collect all matching devices, pick the one with strongest signal
        candidates = []
        for addr, (dev, adv) in devices.items():
            name = adv.local_name or dev.name or ""
            if name in KNOWN_BLE_NAMES:
                rssi = adv.rssi if hasattr(adv, 'rssi') else -999
                candidates.append((rssi, name, addr))
        if not candidates:
            return (None, None)
        # Sort by RSSI descending (strongest signal first)
        candidates.sort(key=lambda x: x[0], reverse=True)
        _, best_name, best_addr = candidates[0]
        return (best_name, best_addr)

    try:
        result = asyncio.run(_scan())
    except Exception:
        pass
    _ble_cache = result
    return result


@pytest.fixture
def flush_serial(serial_port):
    """Flush serial buffers before each test."""
    serial_port.reset_input_buffer()
    serial_port.reset_output_buffer()


@pytest.fixture
def args():
    """Provide dummy argparse args for test functions."""
    # BLE name: env override (OPENTERFACE_BLE_NAME=KeyMod|KVM-GO|...),
    # otherwise auto-detected from live advertising.
    device_name = os.environ.get("OPENTERFACE_BLE_NAME")
    ble_addr = os.environ.get("OPENTERFACE_BLE_ADDR") or None
    if not device_name:
        detected_name, detected_addr = _detect_ble_device()
        device_name = detected_name or KNOWN_BLE_NAMES[0]
        ble_addr = ble_addr or detected_addr
    return argparse.Namespace(
        device_name=device_name,
        ble_addr=ble_addr,
        target="192.168.11.2",
        port=22,
        local_port=8022,
        verbose=False,
        ack_window=2,
        data_only=False,
        test_only=True,
    )


@pytest.fixture
def client():
    """Provide dummy BleEthClient for test functions."""
    pytest.skip("BLE client fixture not implemented")


@pytest.fixture
def results():
    """Provide dummy TestResults for test functions."""
    class DummyResults:
        def record_pass(self, name):
            print(f"  PASS: {name}")
        def record_fail(self, name, reason):
            # Raise so pytest reports the failure instead of silently passing.
            raise AssertionError(f"{name}: {reason}")
    return DummyResults()


@pytest.fixture
def runner():
    """Provide dummy TestRunner for test_ch9329_ecm_full.py."""
    # Import here to avoid circular imports
    try:
        from tests.test_ch9329_ecm_full import TestRunner
        return TestRunner()
    except ImportError:
        pytest.skip("TestRunner not available")
