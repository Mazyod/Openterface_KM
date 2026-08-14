# BLE-Eth Tunnel Usage Examples

Practical examples for `tools/ble_eth_tunnel.py`, the Python client library
for the BLE-to-Ethernet tunnel protocol. For the wire protocol itself see
[ble-eth-protocol.md](ble-eth-protocol.md); for SSH-oriented workflows see
[ble-eth-ssh-usage.md](ble-eth-ssh-usage.md).

All examples were validated against a `KeyMod` device tunneling to a macOS
SSH server at `192.168.11.2:22`.

## Prerequisites

```bash
source .venv_ecm/bin/activate   # or any venv with the deps
python3 -m pip install -r tools/requirements.txt
```

Run from the repository root so `tools.ble_eth_tunnel` is importable:

```python
import sys
sys.path.insert(0, ".")
from tools.ble_eth_tunnel import BleEthClient, BleTcpProxy
```

Validated environment:

| Item | Value |
|------|-------|
| BLE device name | `KeyMod` (or `KVM-GO` / `Mini-KVM` per board) |
| Tunnel target | `192.168.11.2:22` (Target PC SSH) |
| ACK window | `strict_data_ack_window_size=2` |

## Example 1: Minimal Tunnel (Read SSH Banner)

The smallest end-to-end check: connect over BLE, open a TCP tunnel to the
target's SSH port, and read the server banner.

```python
import asyncio
from tools.ble_eth_tunnel import BleEthClient

async def main():
    client = BleEthClient(
        device_name="KeyMod",
        ble_address="1CC44426-51DA-E45A-73C8-2B01006A2D1E",  # optional
        strict_data_ack_window_size=2,
    )
    await client.connect()

    conn_id = await client.open_tunnel("192.168.11.2", 22, timeout=10)
    print("tunnel opened:", conn_id)

    banner = await client.recv_data(conn_id, timeout=10)
    print("banner:", banner)          # b'SSH-2.0-OpenSSH_10.2\r\n'

    await client.close_tunnel(conn_id)
    await client.disconnect()

asyncio.run(main())
```

`open_tunnel()` only returns after the firmware completes ARP + the TCP
three-way handshake on the target side, so the banner is already in flight
when it returns.

## Example 2: Local TCP Proxy for Any Client

`BleTcpProxy` accepts local TCP connections and forwards each one through a
fresh BLE tunnel. This lets stock tools (`ssh`, `scp`, `nc`) use the tunnel
without any code changes.

```python
import asyncio
from tools.ble_eth_tunnel import BleEthClient, BleTcpProxy

async def main():
    client = BleEthClient(device_name="KeyMod")
    await client.connect()
    info = await client.get_info()

    proxy = BleTcpProxy(
        client=client,
        local_port=8022,
        target_ip="192.168.11.2",
        target_port=22,
        max_connections=info.get("max_connections", 2),
    )
    await proxy.start()   # listens on 127.0.0.1:8022

    # In another terminal: ssh -p 8022 user@127.0.0.1
    await asyncio.Event().wait()   # run forever

asyncio.run(main())
```

Key proxy behaviors:

- Each incoming local connection opens a new tunnel (`CONNECT`) under a
  connection lock; CONNECT failures (`0xE1` timeout, `0xE7` full) are
  retried up to `connect_retries` times after cleaning stale tunnel slots.
- Forwarding is bidirectional until either side closes; the tunnel is then
  released with `DISCONNECT`.
- `recv_timeout` (default 30 s) closes idle tunnels instead of hanging.

## Example 3: Automated SSH Command Execution

Run a full non-interactive SSH session through the tunnel from Python.
The SSH subprocess must run in a thread executor — a blocking
`subprocess.run()` in the async body would freeze the event loop and the
proxy could never accept the SSH connection.

```python
import asyncio
import subprocess
from tools.ble_eth_tunnel import BleEthClient, BleTcpProxy

async def main():
    client = BleEthClient(device_name="KeyMod")
    await client.connect()
    info = await client.get_info()

    proxy = BleTcpProxy(
        client=client, local_port=8022,
        target_ip="192.168.11.2", target_port=22,
        max_connections=info.get("max_connections", 2),
    )
    await proxy.start()
    await asyncio.sleep(1)

    loop = asyncio.get_event_loop()

    def run_ssh():
        return subprocess.run(
            ["ssh", "-p", "8022",
             "-o", "ConnectTimeout=15",
             "-o", "StrictHostKeyChecking=no",
             "-o", "UserKnownHostsFile=/dev/null",
             "-o", "BatchMode=yes",
             "user@127.0.0.1",
             "echo OK && uname -a"],
            capture_output=True, text=True, timeout=30,
        )

    # Thread executor keeps the event loop free to serve the proxy.
    result = await asyncio.wait_for(loop.run_in_executor(None, run_ssh), 35)
    print(result.returncode, result.stdout)

    await proxy.stop()
    await client.disconnect()

asyncio.run(main())
```

This is exactly what `tests/test_ble_eth_ssh.py::test_ble_eth_ssh_command_execution`
does; a full session (key exchange, auth, command) completes in about 4 s.

## Example 4: Link Health Check (INFO / PING)

`get_info()` reads firmware capabilities; `ping()` exercises the BLE data
path without touching the Ethernet side.

```python
client = BleEthClient(device_name="KeyMod")
await client.connect()

info = await client.get_info()
# {'version': 1, 'max_connections': 2, 'max_fragment_payload': 246,
#  'features': 15, 'data_ack_window_supported': True, ...}

pong = await client.ping(b"hello", timeout=5.0)   # echoed payload
stats = client.get_reliability_stats()            # ACK/retransmit counters
```

## Message Size and MSS Constraints

Important limits when sending data through a tunnel:

- One BLE fragment carries at most 246 payload bytes
  (`MAX_PAYLOAD_LEN 249 - 3` frag header).
- One tunnel message is at most 6 fragments. The host library caps a single
  message at `MAX_MESSAGE_LEN = 1440` bytes.
- The firmware forwards each reassembled message as TCP segment(s) toward
  the target. Segments are capped at MSS 1460 (Ethernet MTU 1500);
  `BLE_Eth_SendDataToEth()` splits larger messages automatically.
  Historically an oversized single segment exceeded MTU and was dropped
  silently, stalling SSH key exchange — keep messages ≤ 1460 bytes.
- `client.send_data(conn_id, data)` handles fragmentation, ACK-window
  pacing, and retransmission internally; just pass arbitrarily sized
  `bytes`.

## Troubleshooting

| Symptom | Likely cause / action |
|---------|----------------------|
| `open_tunnel` times out (`0xE1`) | Target not answering SYN; check target IP/port, retry after power-cycle |
| All fragments ACKed but no reply data | Legacy firmware dropping >MSS segments; update firmware or keep messages ≤ 1440 |
| Proxy never logs `Local connection from ...` | Event loop blocked by synchronous code; move subprocess calls to `loop.run_in_executor` |
| Wrong device picked during scan | Multiple advertisers (`KeyMod` vs far-away `KVM-GO`); pass `ble_address` explicitly or select by strongest RSSI |
| `BLE recv timeout for conn_id=N` | Idle tunnel closed by `recv_timeout`; reconnect |
