# Tools

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
