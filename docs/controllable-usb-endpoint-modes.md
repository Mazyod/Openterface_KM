# Controllable USB Endpoint Modes

## Goal

Build one firmware architecture that can expose one of several paired USB personalities across the two physical USB device controllers:

1. **Mode 1: HID + host CDC ACM**
   - Target side: keyboard, mouse, absolute/stylus pointer, media keys
   - Host side: CDC ACM

2. **Mode 2: HID + UAC bridge + host CDC ACM**
   - Target side: keyboard, mouse, absolute/stylus pointer, media keys, UAC target audio
   - Host side: CDC ACM, UAC host audio
   - Bridge: existing UAC audio bridge

3. **Mode 3: HID + target CDC ACM + host CDC ACM**
   - Target side: keyboard, mouse, absolute/stylus pointer, media keys, CDC ACM
   - Host side: CDC ACM
   - Bridge: raw ACM data bridge

4. **Mode 4: HID + target CDC ECM + host CDC ECM**
   - Target side: keyboard, mouse, absolute/stylus pointer, media keys, CDC ECM
   - Host side: CDC ECM
   - Bridge: Ethernet frame bridge

The static build profiles are implemented for all four modes. The runtime-capable image is the Mode 4 build: it can boot into stored Mode 1, Mode 2, Mode 3, or Mode 4 by selecting active descriptors, endpoints, class request handlers, and bridge polling at startup. Mode 2 adds UAC to the runtime binary, so this image is larger and uses more RAM than the non-UAC runtime build.

For flash, ROM, SRAM, stack, heap, and persistent config-page details, see `docs/internal-memory-usage.md`.

## Existing USB Topology

The firmware uses two USB device stacks:

- **Target side USBFS**: descriptors in `APP/usbd_desc.c`, endpoint setup and EP0 handling in `APP/ch32v20x_usbfs_device.c`.
- **Host side USBD**: descriptors in `APP/USBLIB/CONFIG/usb_desc.c`, class/standard request handling in `APP/USBLIB/CONFIG/usb_prop.c`, endpoint callbacks in `APP/USBLIB/CONFIG/usb_endp.c`.

The target-side HID baseline currently uses:

| Function | Interface | Endpoint |
| --- | --- | --- |
| Keyboard | IF0 | EP1 IN `0x81` |
| Relative mouse | IF1 | EP2 IN `0x82` |
| Absolute mouse or stylus | IF2 | EP3 IN `0x83` |
| Media keys | IF3 | EP4 IN `0x84` |

When UAC is enabled on the target side, it also uses:

| Function | Interface | Endpoint |
| --- | --- | --- |
| AudioControl | IF4 | none |
| AudioStreaming IN | IF5 | EP5 ISO IN `0x85` |
| AudioStreaming OUT | IF6 | EP6 ISO OUT `0x06` |

The host-side CDC ACM baseline uses:

| Function | Interface | Endpoint |
| --- | --- | --- |
| CDC notification | IF0 | EP1 interrupt IN `0x81` |
| CDC data OUT | IF1 | EP5 bulk OUT `0x05` |
| CDC data IN | IF1 | EP3 bulk IN `0x83` |

When UAC is enabled on the host side, the existing bridge also uses USBD audio endpoints defined in the host-side descriptors and handled by `APP/uac_handler.c`.

## Endpoint Policy

Target HID endpoints stay stable in every mode:

- EP1 IN: keyboard
- EP2 IN: relative mouse
- EP3 IN: absolute mouse or stylus
- EP4 IN: media keys

Modes 3 and 4 must not enable UAC, because target-side CDC ACM and CDC ECM need the endpoint numbers and buffers currently consumed by UAC.

Recommended target-side endpoint allocation for future non-UAC modes:

| Mode | Endpoint | Use |
| --- | --- | --- |
| Mode 3 | EP5 interrupt IN | target CDC ACM notification |
| Mode 3 | EP6 bulk OUT/IN or EP6 OUT + EP7 IN | target CDC ACM data |
| Mode 4 | EP5 interrupt IN | target CDC ECM notification |
| Mode 4 | EP6 bulk OUT + EP7 bulk IN | target CDC ECM data |

The exact EP6/EP7 split must be validated against the CH32V208 USBFS endpoint mode registers and DMA buffer layout before descriptors are finalized.

## Build-Time Mode Model

Introduce one user-facing build variable:

```make
USB_WORK_MODE ?= hid_uac_acm
```

Supported values:

| Value | Numeric define | Meaning |
| --- | --- | --- |
| `hid_acm` | `1` | Mode 1 |
| `hid_uac_acm` | `2` | Mode 2 |
| `hid_acm_bridge` | `3` | Mode 3 |
| `hid_ecm_bridge` | `4` | Mode 4 |

The mode layer should derive feature flags from the selected work mode:

- `USB_WORK_MODE_HAS_TARGET_UAC`
- `USB_WORK_MODE_HAS_HOST_UAC`
- `USB_WORK_MODE_HAS_TARGET_CDC_ACM`
- `USB_WORK_MODE_HAS_HOST_CDC_ACM`
- `USB_WORK_MODE_HAS_TARGET_CDC_ECM`
- `USB_WORK_MODE_HAS_HOST_CDC_ECM`
- `USB_WORK_MODE_BRIDGE_NONE`
- `USB_WORK_MODE_BRIDGE_UAC`
- `USB_WORK_MODE_BRIDGE_ACM`
- `USB_WORK_MODE_BRIDGE_ECM`

The implementation keeps these static feature macros for normal single-mode builds and adds compile-inclusion macros for the runtime-capable Mode 4 image:

- `USB_WORK_MODE_COMPILES_TARGET_UAC`
- `USB_WORK_MODE_COMPILES_HOST_UAC`
- `USB_WORK_MODE_COMPILES_TARGET_CDC_ACM`
- `USB_WORK_MODE_COMPILES_HOST_CDC_ACM`
- `USB_WORK_MODE_COMPILES_TARGET_CDC_ECM`
- `USB_WORK_MODE_COMPILES_HOST_CDC_ECM`
- `USB_WORK_MODE_COMPILES_BRIDGE_UAC`
- `USB_WORK_MODE_COMPILES_BRIDGE_ACM`
- `USB_WORK_MODE_COMPILES_BRIDGE_ECM`

In a normal static build these match the selected mode. In the Mode 4 runtime-capable build they compile UAC, ACM, and ECM bridge pieces, while runtime helpers in `APP/usb_mode_config.c` decide which descriptor profile and data path is active.

## Runtime-Capable Mode 4 Image

`USB_WORK_MODE_RUNTIME_PROFILES` is currently enabled only when building `hid_ecm_bridge` / numeric Mode 4. That image supports these stored runtime modes:

| Stored mode | Active target profile | Active host profile | Bridge |
| --- | --- | --- | --- |
| Mode 1 | HID only | CDC ACM | none |
| Mode 2 | HID + UAC | CDC ACM + UAC | UAC audio bridge |
| Mode 3 | HID + CDC ACM | CDC ACM | ACM byte bridge |
| Mode 4 | HID + CDC ECM | CDC ECM | ECM frame bridge |

Expected HID Feature report capability mask for this image is `0x0000001E`, which is bits 1, 2, 3, and 4.

Mode changes are persistent but take effect after reset or replug because USB descriptors are selected during enumeration:

```powershell
python tools/test_usb_mode_hid_windows.py --write-mode 3
python tools/test_usb_mode_hid_windows.py --reboot
```

## Implementation Phases

### Phase 1: Foundation

1. Add `APP/include/usb_work_mode.h` with mode IDs and derived feature macros.
2. Add `USB_WORK_MODE` to `build/makefile` and stamp it in `.build-config.stamp`.
3. Update `tools/build_firmware.ps1` to accept `-UsbWorkMode`.
4. Replace direct UAC feature decisions with work-mode derived macros where practical.
5. Keep the default behavior equivalent to current firmware: Mode 2, HID + UAC bridge + host CDC ACM.
6. Build Mode 1 and Mode 2 successfully.

### Phase 2: Descriptor Profile Split

1. Split target descriptors into profile-owned tables:
   - target HID only
   - target HID + UAC
   - target HID + CDC ACM
   - target HID + CDC ECM
2. Split host descriptors into profile-owned tables:
   - host CDC ACM
   - host CDC ACM + UAC
   - host CDC ECM
3. Route descriptor pointer/length selection through a small profile descriptor table.
4. Preserve size checks for each descriptor table.

### Phase 3: Endpoint Setup Split

1. Move target USBFS endpoint setup into mode-specific helpers.
2. Move host USBD endpoint reset/setup into mode-specific helpers.
3. Ensure inactive endpoints are disabled or NAKed and never leak from another mode.
4. Add mode-specific class request dispatch on EP0.

### Phase 4: Mode 3 ACM Bridge

1. Add target-side CDC ACM descriptors and class requests. Done as the first Mode 3 scaffold.
2. Add target USBFS CDC ACM endpoint callbacks. Done for enumeration and basic endpoint re-arm.
3. Add target CDC ACM RX/TX queues. Done in `APP/acm_bridge.c`.
4. Add an ACM bridge module that moves bytes between target CDC ACM and host CDC ACM. Done and validated with bidirectional Windows serial transfer tests.
5. Keep CH9329 command/response traffic separate from bridge traffic unless explicitly routed.
6. Verify with `tools/test_acm_bridge_windows.ps1` using small packets first, then sustained serial streams.

### Phase 5: Mode 4 ECM Bridge

1. Add target-side CDC ECM descriptors and class requests. Initial enumeration scaffold done.
2. Add host-side CDC ECM descriptors and class requests. Initial enumeration scaffold done.
3. Add Ethernet frame queues with explicit frame boundaries. Done in `APP/ecm_bridge.c`.
4. Implement packet filter state, MAC string handling, and basic statistics. Basic packet-filter state, MAC strings, zeroed statistics responses, and link notifications are in place.
5. Add host-side TCP command interception for CH9329-framed payloads. Done in `APP/ecm_bridge.c`: host-to-device Ethernet frames are parsed as IPv4/TCP, payloads made entirely of valid CH9329 frames are routed into the HID control parser, and nonmatching frames continue through the bridge.
6. Verify enumeration on ECM-capable hosts first. Stock Windows is expected not to bind CDC ECM as a NIC.

#### Mode 4 Bring-up Findings (macOS, 2026-06)

These notes capture the root causes and fixes that mattered for getting CDC ECM mode stable on macOS (ARP resolution + TCP reachability). They’re intentionally behavior-first and point at the relevant subsystems rather than preserving every incremental debug log.

Known-good configuration (example):

- Runtime mode: `USB_WORK_MODE_HID_ECM_BRIDGE` (mode `4`)
- macOS interface: `enXX` (for example `en20`)
- Host IP: `192.168.11.1/24`
- Firmware synthetic peer:
  - IP: `192.168.11.3`
  - MAC used in ARP replies and as Ethernet src: `02:00:00:00:00:31`
- CDC ECM descriptor MAC string (iMACAddress=4): `020000000030` (i.e. `02:00:00:00:00:30`)

Root causes and fixes:

1. **EP7 IN stuck before the ECM data altsetting is active**
   - Do not pre-queue ECM TX before the host selects the data interface altsetting.
   - On `USB_SET_INTERFACE` for the ECM data interface, clear EP7 TX state (`UEP7_TX_LEN`, `USBFS_Endp_Busy[DEF_UEP7]`) and reset `g_garp_sent`.
   - Defer GARP until the data interface is active.

2. **RX frame reassembly can be “poisoned” by unsupported short frames**
   - Unknown short frames must be consumed/dropped so the reassembly state cannot stall forever.
   - IPv6 frame-length handling needs to be exact.

3. **Unhandled target-side frames were being reflected back**
   - Ensure target-side frames are always consumed (even if unhandled) to avoid echo loops on the ECM link.

4. **ARP reply MAC conflict with the interface identity**
   - macOS can reject ARP replies if the firmware answers with the same MAC as the interface descriptor.
   - Keep descriptor MAC (`...:30`) and firmware peer MAC (`...:31`) distinct.

Diagnostics:

- HID Feature report `0x30` includes ECM diagnostics (data IF active, GARP state, EP6 RX count, EP7 TX/busy state, EP7 IN completion count).
- On macOS, `python3 tools/usb_mode_switch_mac.py` prints the status + diagnostics (target-facing USBFS path needs no sudo).

Bring-up validation:

```bash
# ARP is the first “real” signal of ECM health
arp -an | grep 192.168.11.3

# TCP reachability to the target service (SSH)
nc -vz -G 2 192.168.11.1 22
```

Notes:

- `ping 192.168.11.3` is not a meaningful failure signal unless ICMP echo is implemented; use ARP resolution + TCP/SSH banner tests instead.

### Phase 6: Persistent Runtime Selection

Add persistent mode selection only after static profile builds work.

1. Reserve one flash page in the linker script. Done: `HAL/Link.ld` keeps the last 4 KB at `0x0006F000` out of the firmware image; firmware accesses the same page through flash alias `0x0806F000`.
2. Store a small config record with magic, version, selected mode, flags, sequence, and CRC. Done in `APP/usb_mode_config.c`.
3. Read the config before `Set_USBConfig()`, `USB_Init()`, and `USBFS_Device_Init()` in `APP/peripheral_main.c`. Done.
4. Expose a management command over the existing CH9329 command path. Done as command `0x30`:
   - no payload or payload `00`: read status
   - payload `01 <mode> [flags_lo flags_hi]`: write persistent mode record if the running firmware supports that mode
   - payload `02`: erase the persistent mode record
   - payload `03`: request `NVIC_SystemReset()`
5. Add a Windows-accessible control path that does not depend on CDC ACM or ECM binding. Done as vendor HID Feature report `0x30` on the target media HID interface:
   - report byte 0: report ID `0x30`
   - report byte 1: command/status (`00` read, `01` write mode, `02` clear record, `03` request reset)
   - report byte 2: mode for write commands
   - report bytes 3-4: flags for write commands
   - readback bytes mirror the CH9329 status payload: last status, compiled mode, active mode, stored mode, config status, flags, and capability mask
   - Windows test tool: `python tools/test_usb_mode_hid_windows.py`
6. Add a hardware safe-mode override that forces Mode 1 so a broken USB mode cannot trap the device. Still pending.

Phase 6 provides persistent storage, boot-time validation, CH9329 command plumbing, a Windows-accessible HID Feature report, and delayed reset support.

### Phase 7: First Runtime Profile Split

The first runtime profile split is implemented in the Mode 4 build. It now includes UAC for Mode 2 as well as the non-UAC Mode 1, Mode 3, and Mode 4 profiles.

1. Add runtime support checks and capability reporting for stored modes 1, 2, 3, and 4. Done in `APP/usb_mode_config.c`.
2. Build target-side USBFS configuration descriptors at startup from a stable HID base plus optional UAC, CDC ACM, or CDC ECM blocks. Done in `APP/usbd_desc.c`.
3. Select host-side USBD configuration descriptors between CDC ACM, CDC ACM + UAC, and CDC ECM at enumeration time. Done in `APP/USBLIB/CONFIG/usb_desc.c`.
4. Gate target endpoint setup, string descriptor access, class request handling, reset-time bridge init, and EP6 OUT dispatch by the active mode. Done in `APP/ch32v20x_usbfs_device.c`.
5. Gate host descriptor callbacks, interface settings, class requests, reset-time bridge init, and EP5 OUT dispatch by the active mode. Done in `APP/USBLIB/CONFIG/usb_prop.c` and `APP/USBLIB/CONFIG/usb_endp.c`.
6. Gate main-loop bridge polling by the active mode. Done in `APP/peripheral_main.c`.
7. Validate on hardware after flashing the runtime-capable Mode 4 image. Mode 4 -> Mode 3 -> Mode 4 was validated before Mode 2 was added: the flashed image reported capability mask `0x0000001A`, exposed host and target CDC ACM COM ports in Mode 3, passed focused bidirectional serial bridge transfer tests, and switched back to Mode 4. The Mode 2-capable build now reports capability mask `0x0000001E`; after HID write/reboot to Mode 2, Windows reports active/stored mode 2, host CDC ACM `COM8`, and both `Host Audio` and `Target Audio` with OK status.

Runtime mode-switch matrix test:

```powershell
python tools/test_usb_mode_switch_matrix_windows.py --plan cycle
python tools/test_usb_mode_switch_matrix_windows.py --plan matrix
python tools/test_usb_mode_switch_matrix_windows.py --plan cycle --functional-tests smoke
python tools/test_usb_mode_switch_matrix_windows.py --plan cycle --modes 3 --functional-tests full --acm-baud-rates 115200 --acm-payload-sizes 16 --acm-iterations 1
python tools/test_usb_mode_switch_matrix_windows.py --plan cycle --modes 2,3 --control-path ch9329
python tools/test_usb_mode_switch_matrix_windows.py --plan matrix --modes 1,2,3,4 --no-restore
```

The script writes each requested stored mode through HID Feature report `0x30`, requests a reboot, waits for the target HID report to return with the expected active/stored mode, and checks Windows enumeration for the expected CDC ACM and UAC devices. The default `matrix` plan tests every ordered source-to-target transition across modes 1, 2, 3, and 4, then restores the initial mode unless `--no-restore` is passed.

The same matrix script can test CH9329 command `0x30` over the host CDC ACM path by passing `--control-path ch9329`. Source setup and final restore still use HID so the test can start from a known source mode and recover after switching into Mode 4. The actual transition under test is written through the host CDC ACM COM port, VID_1A86 PID_FE0C. This path is only available while the currently active source mode exposes CDC ACM; stock Mode 4 exposes CDC ECM instead, so a Mode 4 source cannot send CH9329 commands on Windows.

Direct CH9329 helper:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ./tools/test_usb_mode_ch9329_windows.ps1 -Operation Read
powershell -NoProfile -ExecutionPolicy Bypass -File ./tools/test_usb_mode_ch9329_windows.ps1 -Operation Write -Mode 3 -NoResponse
powershell -NoProfile -ExecutionPolicy Bypass -File ./tools/test_usb_mode_ch9329_windows.ps1 -Operation Reboot -NoResponse
```

The direct helper can wait for a CDC response frame, but the validated mode-switch test uses `-NoResponse` for write/reboot and verifies the result through the HID status report after re-enumeration. This avoids depending on CDC response delivery while still proving that command `0x30` reached the firmware and changed the persistent stored mode.

The BLE rawpass path can also carry full CH9329 command `0x30` frames on service `0xFFF0`, RX characteristic `0xFFF2`. Firmware routes complete mode command frames directly into the CH9329 parser before normal BLE-to-USB forwarding can consume them. Hardware retest passed Mode 2 -> Mode 3 and Mode 3 -> Mode 2 using BLE writes for the mode/reboot frames, with HID readback confirming active/stored modes after each re-enumeration. BLE status responses are still not treated as a validated readback path; use HID after reboot for verification. The Windows BLE helper accepts BLE advertisements whose name contains `KVM-GO` or `KeyMod`, and also falls back to matching service UUID `0xFFF0`.

Direct BLE helper:

```powershell
python tools/test_usb_mode_ble_windows.py --write-mode 3 --reboot --verify
python tools/test_usb_mode_ble_windows.py --write-mode 2 --reboot --verify
```

Functional test levels:

- `--functional-tests smoke` adds noninvasive checks after each mode switch. Mode 1 confirms HID control readback and the host CDC ACM management port; Mode 2 confirms PortAudio can list devices and both UAC media functions are present; Mode 3 confirms the bridge COM ports are discoverable; Mode 4 confirms HID readback and notes that stock Windows does not run a CDC ECM data-path test.
- `--functional-tests full` runs available data-path tests. Mode 3 calls `tools/test_acm_bridge_windows.ps1` with configurable baud rates, payload sizes, iterations, and timeouts. Mode 2 runs `tools/audio_jitter_test.py` with explicit or auto-picked audio input/output indices; use `--audio-output`, `--audio-input`, `--audio-output-name`, `--audio-input-name`, `--audio-duration`, and `--audio-report` to control it.
- Stock Windows usually cannot bind Mode 4 CDC ECM as a network adapter. `--require-ecm-net` can be used on hosts where an ECM-capable driver is installed.

## Verification Matrix

| Mode | Build check | Enumeration check | Data check |
| --- | --- | --- | --- |
| Mode 1 | build `USB_WORK_MODE=hid_acm` | target HID only, host COM port | HID reports, host CDC command path |
| Mode 2 | build `USB_WORK_MODE=hid_uac_acm` | target HID + audio, host COM + audio | existing UAC bridge test |
| Mode 3 | build `USB_WORK_MODE=hid_acm_bridge` | target COM, host COM | `tools/test_acm_bridge_windows.ps1` |
| Mode 4 | build `USB_WORK_MODE=hid_ecm_bridge` | target ECM, host ECM | Ethernet frame bridge |
| Runtime Mode 4 image | build `USB_WORK_MODE=hid_ecm_bridge` | stored modes 1/2/3/4 select different descriptors after reset | `tools/test_usb_mode_switch_matrix_windows.py --functional-tests smoke`, `--control-path ch9329` from CDC ACM source modes, Mode 3 serial bridge, Mode 2 audio diagnostic, ECM-capable host for Mode 4 |

## Risks

- USBFS endpoint mode and DMA buffer layout may constrain the exact endpoint numbers for target CDC ACM and ECM.
- Host driver caching may require distinct PIDs per mode to make repeated testing reliable.
- CDC ECM is not a Windows-friendly networking function. RNDIS or NCM may be needed later for a usable Windows NIC.
- Persistent mode switching must include a safe fallback before it is enabled in normal builds.
- The existing CH9329 CDC ACM command path should not be mixed with bridge payloads without an explicit framing decision.

## Current First Implementation Target

Current implementation status:

- The named work-mode build flag is in place.
- Mode 1 and Mode 2 build successfully.
- Mode 2 remains the default.
- Mode 3 builds with target CDC ACM descriptors, class requests, endpoint re-arm, and byte bridge forwarding.
- Mode 3 has a Windows serial bridge test script at `tools/test_acm_bridge_windows.ps1`.
- Mode 3 passed Windows transfer validation with `tools/test_acm_bridge_windows.ps1` across 5 baud rates, 4 payload sizes, 2 iterations, and both directions.
- Mode 4 now builds with target/host CDC ECM descriptors, MAC address strings, basic ECM EP0 request handling, bulk endpoint setup, network-connection/speed-change notifications, and a frame-preserving bridge queue between target EP6/EP7 and host EP5/EP3.
- Mode 4 flashed on Windows enumerates target HID interfaces successfully and exposes target/upstream CDC ECM control interfaces as `USB\Class_02&SubClass_06&Prot_00`; Windows reports failed driver install and no network adapter, as expected for stock CDC ECM.
- Mode 4 frame forwarding still needs validation on an ECM-capable host because stock Windows does not bind a CDC ECM network adapter.
- Persistent mode storage and HID Feature report control are implemented and hardware-tested for read/write/reset control.
- The runtime-capable Mode 4 image now builds with Mode 2 included and advertises capability mask `0x0000001E` for modes 1, 2, 3, and 4.
- Runtime descriptor/endpoint switching is hardware-validated for Mode 4 -> Mode 3 -> Mode 4 and for Mode 4 -> Mode 2. Mode 3 exposed host `COM6` and target `COM7`, and the ACM bridge passed 18 bidirectional transfers across 9600, 115200, and 921600 baud with 16, 64, and 255 byte payloads. Mode 2 exposes host CDC ACM `COM8`, `Host Audio`, and `Target Audio` with OK status on Windows.
- Runtime mode-switch matrix test script added at `tools/test_usb_mode_switch_matrix_windows.py`. Smoke-tested Mode 2 -> Mode 2 and Mode 2 -> Mode 3 cycles successfully on Windows. Functional testing support is added: Mode 2 smoke confirms PortAudio-visible UAC devices, and Mode 3 full test passed host-to-target and target-to-host ACM transfer at 115200 baud with 16-byte payloads.
- CH9329 command `0x30` Windows test helper added at `tools/test_usb_mode_ch9329_windows.ps1`, and the matrix script supports `--control-path ch9329`. Firmware now routes host CDC ACM command frames to the CH9329 parser in non-bridge CDC modes and recognizes valid command `0x30` frames before normal Mode 3 bridge forwarding. Hardware retest passed Mode 2 -> Mode 3 and Mode 3 -> Mode 2 using CH9329 write/reboot one-way commands, with HID readback confirming active/stored modes after re-enumeration.
- BLE rawpass mode-switch command delivery is hardware-tested. BLE writes to service `0xFFF0` RX characteristic `0xFFF2` switched Mode 2 -> Mode 3 and Mode 3 -> Mode 2 with CH9329 command `0x30` frames; HID readback confirmed both transitions after reboot. The Windows helper is `tools/test_usb_mode_ble_windows.py` and scans for `KVM-GO`, `KeyMod`, or service UUID `0xFFF0`.