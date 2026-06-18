# ETH Bridge Test Suite

Test script: `tools/test_eth_bridge.py`

## Overview

Tests the complete ETH bridge path between host (Mac/PC) and target through Openterface firmware:

```
Host Mac/PC <--> BLE <--> Firmware <--> USB CDC-ECM <--> Target PC
```

The current validated configuration:
- **Host**: Mac at `192.168.11.1` (via USBD EP3)
- **Target PC**: at `192.168.11.2` (via USBFS EP7)
- **SSH test target**: `192.168.11.2:22`

## Architecture

### IP Configuration

| Side | Role | IP Address | MAC Address |
|------|------|------------|-------------|
| Host (USBD EP3) | DHCP client | 192.168.11.1 | 02:00:00:00:00:02 |
| Firmware (Host side) | DHCP server | 192.168.11.3 | 02:00:00:00:00:32 |
| Firmware (Target side) | DHCP server | 192.168.11.5 | 02:00:00:00:03:00 |
| Target (USBFS EP7) | DHCP client | 192.168.11.2 | (assigned by firmware) |

### Protocol Flow

1. **BLE Connection**: Host connects to device via BLE (bleak library)
2. **INFO Exchange**: Query device capabilities (max connections, features)
3. **PING/PONG**: Test BLE-Eth round-trip (no network needed)
4. **TCP Tunnel**: CONNECT to target IP:port through BLE
5. **SSH Banner**: Verify end-to-end connectivity

## Test Cases

### Protocol Frame Tests (no BLE needed)

| Test | Description |
|------|-------------|
| `CONNECT frame build/parse` | Build CONNECT frame (0x10) with IP:port, verify parsing |
| `INFO frame build/parse` | Build INFO request (0x1F), verify parsing |
| `DISCONNECT frame build/parse` | Build DISCONNECT frame (0x12), verify parsing |
| `PING frame build/parse` | Build PING frame (0x1E) with payload, verify parsing |
| `Checksum validation rejects bad frames` | Verify parser rejects frames with invalid checksum |
| `Parser resync after garbage` | Verify parser recovers after garbage data |

### BLE Connection Tests

| Test | Description |
|------|-------------|
| `BLE connect` | Connect to device via BLE (scan + connect) |
| `INFO exchange` | Query device info (version, max_connections, features) |

### PING/PONG Tests (no network needed)

| Test | Description |
|------|-------------|
| `Simple PING/PONG (5 bytes)` | Round-trip 5-byte payload |
| `Large PING/PONG (247 bytes)` | Round-trip 247-byte payload (near max fragment) |
| `Max PING/PONG (249 bytes)` | Round-trip 249-byte payload (max payload) |

### TCP Tunnel Tests (requires target)

| Test | Description |
|------|-------------|
| `CONNECT to 192.168.11.2:22` | Open TCP tunnel through BLE to target |
| `SSH banner exchange through tunnel` | Read SSH banner through tunnel |

### ECM Bridge Diagnostics

| Test | Description |
|------|-------------|
| `ECM bridge configuration` | Log ECM bridge IP/MAC configuration |

## Usage

```bash
# Full bridge test (requires target PC connected)
python3 tools/test_eth_bridge.py

# BLE + PING only (no target needed)
python3 tools/test_eth_bridge.py --ble-only

# Test specific target IP
python3 tools/test_eth_bridge.py --target 192.168.11.2 --port 22

# Verbose logging
python3 tools/test_eth_bridge.py -v
```

## Test Results

### BLE-Only Mode (no target needed)

```
============================================================
ETH Bridge Test Suite
============================================================
  Target:      192.168.11.2:22
  BLE device:  KVM-GO
  Mode:        BLE-only

--- Protocol Frame Tests ---
  [PASS] CONNECT frame build/parse
  [PASS] INFO frame build/parse
  [PASS] DISCONNECT frame build/parse
  [PASS] PING frame build/parse
  [PASS] Checksum validation rejects bad frames
  [PASS] Parser resync after garbage

--- BLE Connection Tests ---
  [PASS] BLE connect
  [PASS] INFO exchange

--- PING/PONG Tests ---
  [PASS] Simple PING/PONG (5 bytes)
  [PASS] Large PING/PONG (247 bytes)
  [PASS] Max PING/PONG (249 bytes)

============================================================
TEST SUMMARY
============================================================
  Passed:  9
  Failed:  0
  Skipped: 0
============================================================
```

### Full Bridge Mode (requires target)

Adds TCP tunnel and SSH banner tests. If target is unreachable:

```
--- TCP Tunnel Tests (192.168.11.2:22) ---
  [FAIL] CONNECT to 192.168.11.2:22: STATUS_TIMEOUT (0xE1) - target unreachable
```

## Troubleshooting

### STATUS_TIMEOUT (0xE1)

Firmware cannot reach target IP. Check:
1. Target PC is connected and powered
2. Target has DHCP client enabled (or static IP 192.168.11.2)
3. Ethernet cable is connected to firmware's USBFS port
4. Target firewall allows SSH (port 22)

### BLE Connect Fails

1. Ensure device is powered and advertising as "KVM-GO"
2. Check Bluetooth is enabled on host
3. Verify bleak is installed: `pip install bleak`
4. Try specifying BLE address: `--ble-addr AA:BB:CC:DD:EE:FF`

### PING/PONG Fails

Indicates BLE-Eth protocol issue. Check:
1. Firmware is running correct version (features should include 0x0F)
2. No other BLE connections interfering
3. Device is within BLE range

## Related Files

- `APP/ecm_bridge.c` - Firmware ECM bridge implementation
- `APP/ble_eth_protocol.c` - Firmware BLE-Eth protocol
- `tools/ble_eth_tunnel.py` - Host-side BLE-Eth client
- `tools/ssh_through_ble_eth.py` - SSH wrapper script
