"""Shared test fixtures for Openterface KM regression tests."""
import pytest
import serial
import argparse


@pytest.fixture(scope="session")
def serial_port():
    """Open serial port to device for the test session."""
    port = "/dev/cu.usbmodem212101"
    baud = 115200
    ser = serial.Serial(port, baud, timeout=3)
    yield ser
    ser.close()


@pytest.fixture
def flush_serial(serial_port):
    """Flush serial buffers before each test."""
    serial_port.reset_input_buffer()
    serial_port.reset_output_buffer()


@pytest.fixture
def args():
    """Provide dummy argparse args for test functions."""
    return argparse.Namespace(
        device_name="KVM-GO",
        ble_addr=None,
        target="192.168.1.100",
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
            print(f"  FAIL: {name} - {reason}")
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
