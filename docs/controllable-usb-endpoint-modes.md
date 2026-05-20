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

The first implementation should be build-time selectable. Runtime persistent selection can be added after all static modes enumerate reliably.

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

The initial code change adds this model and maps the existing UAC build flag onto Mode 1 and Mode 2. Mode 3 now has a target-side CDC ACM enumeration scaffold, but the ACM bridge data path is still pending. Mode 4 should fail at compile time until its descriptors and handlers are implemented.

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

1. Add target-side CDC ECM descriptors and class requests.
2. Add host-side CDC ECM descriptors and class requests.
3. Add Ethernet frame queues with explicit frame boundaries.
4. Implement packet filter state, MAC string handling, and basic statistics.
5. Verify enumeration on ECM-capable hosts first. Stock Windows is expected not to bind CDC ECM as a NIC.

### Phase 6: Persistent Runtime Selection

Add persistent mode selection only after static profile builds work.

1. Reserve one flash page in the linker script.
2. Store a small config record with magic, version, selected mode, flags, and CRC.
3. Read the config before `Set_USBConfig()`, `USB_Init()`, and `USBFS_Device_Init()` in `APP/peripheral_main.c`.
4. Change modes by writing flash and rebooting with `NVIC_SystemReset()` or by requiring a replug.
5. Add a hardware safe-mode override that forces Mode 1 so a broken USB mode cannot trap the device.

## Verification Matrix

| Mode | Build check | Enumeration check | Data check |
| --- | --- | --- | --- |
| Mode 1 | build `USB_WORK_MODE=hid_acm` | target HID only, host COM port | HID reports, host CDC command path |
| Mode 2 | build `USB_WORK_MODE=hid_uac_acm` | target HID + audio, host COM + audio | existing UAC bridge test |
| Mode 3 | build `USB_WORK_MODE=hid_acm_bridge` | target COM, host COM | `tools/test_acm_bridge_windows.ps1` |
| Mode 4 | build `USB_WORK_MODE=hid_ecm_bridge` | target ECM, host ECM | Ethernet frame bridge |

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
- Mode 4 remains rejected at compile time until its target/host ECM descriptors, endpoint setup, and bridge handlers exist.