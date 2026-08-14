#!/usr/bin/env python3
"""Test ECM bridge connectivity using ICMP Echo and SSH.

Tests the ECM bridge data path by:
    1. Pinging the Target PC (192.168.11.2) through the bridge
    2. Verifying SSH connectivity to the Target PC

The firmware operates as a pure Ethernet bridge — no diagnostic IP.

Requires:
    - Openterface USB device connected with ECM bridge active

Usage:
    .venv/bin/python -m pytest tests/test_ecm_icmp.py -v
"""

from __future__ import annotations

import subprocess

import pytest

# ---------------------------------------------------------------------------
# ECM bridge network configuration
# ---------------------------------------------------------------------------

TARGET_IP = "192.168.11.2"


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestECM_Bridge:
    """ECM bridge connectivity tests."""

    def test_target_ping_system_level(self):
        """Verify Target PC is reachable via standard system ping.

        This tests the ECM bridge forwarding path: Mac → firmware → Target PC.
        """
        result = subprocess.run(
            ["ping", "-c", "3", "-W", "2", TARGET_IP],
            capture_output=True,
            text=True,
            timeout=15,
        )
        assert result.returncode == 0, (
            f"Ping to Target PC failed:\n{result.stdout}\n{result.stderr}"
        )
        assert "0.0% packet loss" in result.stdout or "0% packet loss" in result.stdout

    def test_target_ssh_connectivity(self):
        """Verify SSH connectivity to Target PC through ECM bridge."""
        result = subprocess.run(
            [
                "ssh", "-o", "ConnectTimeout=5",
                "-o", "StrictHostKeyChecking=no",
                "-o", "BatchMode=yes",
                "pengtianyu@" + TARGET_IP,
                "echo SSH_OK",
            ],
            capture_output=True,
            text=True,
            timeout=15,
        )
        # SSH_OK in stdout or password prompt (means TCP connected)
        connected = (
            "SSH_OK" in result.stdout
            or "password" in result.stderr.lower()
            or result.returncode == 0
        )
        assert connected, (
            f"SSH to Target PC failed:\nstdout: {result.stdout}\nstderr: {result.stderr}"
        )
