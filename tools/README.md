# Tools

BLE-Eth SSH usage

See [docs/ble-eth-ssh-usage.md](../docs/ble-eth-ssh-usage.md) for:

- starting the BLE-Eth proxy
- interactive SSH login
- scripted SSH login through the local forwarded port

BLE-Eth tunnel library examples

See [docs/ble-eth-tunnel-examples.md](../docs/ble-eth-tunnel-examples.md) for
`tools/ble_eth_tunnel.py` usage:

- minimal tunnel (read SSH banner)
- local TCP proxy for stock clients (`ssh`, `scp`)
- automated SSH command execution from Python
- INFO/PING link health checks
- message size / MSS constraints

CDC-ACM mode switch helper

Install dependencies:

```bash
python3 -m pip install -r tools/requirements.txt
```

List candidate ports:

```bash
python3 tools/cdc_acm_switch.py --list
```

Read current USB work mode status (auto-detect port):

```bash
python3 tools/cdc_acm_switch.py --read
```

Set stored mode to 3 and request reboot:

```bash
python3 tools/cdc_acm_switch.py --write 3 --reboot
```

If auto-detect fails, pass `--port /dev/tty.usbmodemXXX` explicitly.

Remote build + local flash helper

This automates the flow from `docs/build-flash-cycle.md`:

- zip and upload project to Windows builder
- run `tools/build_firmware.sh` remotely (MSYS2)
- download `/tmp/openterface-km-firmware.bin`
- enter ISP mode and flash with local `wchisp`
- release BOOT pin, power-cycle, and verify USB enumeration

Examples:

```bash
bash tools/build_flash_cycle.sh
```

```bash
bash tools/build_flash_cycle.sh --control-port /dev/cu.usbmodem212101
```

```bash
bash tools/build_flash_cycle.sh --no-build --local-bin /tmp/openterface-km-firmware.bin
```
