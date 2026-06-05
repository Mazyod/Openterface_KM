# Internal Memory Usage

This firmware targets the CH32V208 / CH32V20x D8W profile used by the active linker script, `HAL/Link.ld`. The build links the application at the MCU flash execution base and reserves one flash page for persistent USB work-mode configuration.

## Memory Map

| Region | Linker origin | Hardware alias | Size | Use |
| --- | --- | --- | --- | --- |
| `FLASH` | `0x00000000` | `0x08000000` | 444 KB | Firmware image: vectors, code, read-only data, and `.data` initializers |
| `USBMODECFG` | `0x0006F000` | `0x0806F000` | 4 KB | Persistent USB work-mode config page, kept out of the firmware image |
| `RAM` | `0x20000000` | same | 64 KB | `.data`, `.bss`, BLE heap, queues, USB buffers, free heap gap, and stack |

The physical flash span treated as application-owned by the firmware is 448 KB total: 444 KB image area plus one 4 KB configuration page. The config page is erased/programmed by `APP/usb_mode_config.c` at `0x0806F000`.

`SRC/Ld/Link.ld` is an older/generated linker script for smaller profiles and is not the active script for the current CH32V208 runtime builds. Use `HAL/Link.ld` as the source of truth.

## Flash / ROM Usage

In build output, "FLASH" or "ROM" usually means bytes that must be present in nonvolatile code flash. For GNU `size`, this is normally:

```text
flash_used = text + data
```

`text` includes executable code and read-only constants. `data` is counted in flash because initialized RAM variables need an initial image stored in flash, then copied into SRAM during startup.

Example runtime-capable Mode 4 build (`USB_WORK_MODE=hid_ecm_bridge`), from `obj/openterface-km-firmware.elf`:

| Region | Used | Capacity | Free / reserved | Used % | Notes |
| --- | ---: | ---: | ---: | ---: | --- |
| Firmware image flash | 215,192 bytes | 454,656 bytes / 444 KB | 239,464 bytes | 47.33% | Linker `FLASH` region, excludes the mode config page |
| Persistent config flash | 16 bytes stored | 4,096 bytes / 4 KB | 4,080 bytes reserved | 0.39% stored | Whole page reserved because erase is page-granular |
| Application-owned flash span | 219,288 bytes allocated | 458,752 bytes / 448 KB | 239,464 bytes | 47.80% | Firmware image plus reserved config page |

ELF section breakdown:

| Item | Bytes | Notes |
| --- | ---: | --- |
| `text` | 214,168 | Code and read-only data |
| `data` | 1,024 | Initialized SRAM data, also stored in flash as load image |
| Flash image used | 215,192 | `text + data`, about 47.33% of the 444 KB image area |

The generated Intel HEX file is larger than flash usage because it is an encoded text format. Use the ELF size numbers, not the `.hex` file length, when judging MCU flash pressure.

## Persistent Flash Page

The last 4 KB flash page is reserved for USB mode selection:

| Field | Value |
| --- | --- |
| Linker region | `USBMODECFG` |
| Linked address | `0x0006F000` |
| Runtime flash address | `0x0806F000` |
| Page size | 4 KB |
| Record size | 16 bytes |
| Stored record | magic, record version, selected mode, flags, sequence, CRC |

Only one 16-byte record is currently stored, but the whole 4 KB page is reserved because flash erase is page-granular. Do not place code, constants, lookup tables, or other settings in this page unless they share the same erase/write policy.

## SRAM Usage

The active linker script gives SRAM from `0x20000000` through `0x2000FFFF`, and reserves a 2 KB stack at the top of RAM.

Example runtime-capable Mode 4 build:

| Region | Used | Capacity | Free | Used % | Notes |
| --- | ---: | ---: | ---: | ---: | --- |
| SRAM reported by `size` | 50,924 bytes | 65,536 bytes / 64 KB | 14,612 bytes | 77.70% | `data + bss`; includes the 2 KB linker stack section |
| Static globals before stack | 48,876 bytes | 63,488 bytes | 14,612 bytes | 76.98% | `.data + .bss` through `_end`, excluding the reserved stack |
| Stack reservation | 2,048 bytes | 2,048 bytes | 0 bytes | 100% reserved | `0x2000F800`-`0x2000FFFF` |

SRAM section placement:

| Item | Address / size | Notes |
| --- | ---: | --- |
| `.data` start | `0x20000000` | Runtime location for initialized globals |
| `.data` size | 1,024 bytes | Copied from flash load address in `FLASH` |
| `.bss` start | `0x20000400` | Zeroed globals start |
| `.bss` before stack | 47,852 bytes | Zeroed globals excluding linker `.stack` |
| `_end` / `_ebss` | `0x2000BEEC` | End of normal static data |
| `_heap_end` | `0x2000F800` | Start of reserved stack area |
| Free heap/growth gap | 14,612 bytes | Space available between `_end` and `_heap_end` |
| Stack | `0x2000F800`-`0x2000FFFF` | 2,048 bytes, set by `__stack_size = 2048` |
| `size` RAM total | 50,924 bytes | `data + bss`; includes the linker `.stack` section |

The build summary reports RAM as `data + bss`, and this linker script emits `.stack` as a NOBITS section, so the reported `bss` includes the 2 KB stack reservation. The practical free area is the gap between `_end` and `_heap_end`, currently about 10.76 KB.

## Major SRAM Consumers

These are the largest current static allocations in the runtime-capable image:

| Symbol / area | Size | Owner | Purpose |
| --- | ---: | --- | --- |
| `MEM_BUF` | 10,240 bytes | `APP/peripheral_main.c` | BLE protocol stack heap (`BLE_MEMHEAP_SIZE=10240`) |
| `kb_queue` | 4,104 bytes | `APP/km_ring.c` | Keyboard report queue (`KB_QUEUE_SIZE=512`) |
| ECM bridge queues + RX frames | 9,128 bytes | `APP/ecm_bridge.c` | `g_host_to_target` + `g_target_to_host` + `g_host_rx_frame` + `g_target_rx_frame` |
| BLE-Eth TX queue | 2,068 bytes | `APP/ble_eth_protocol.c` | `g_tx_queue` |
| BLE-Eth reassembly | 1,544 bytes | `APP/ble_eth_protocol.c` | `g_reassembly` |
| ACM bridge queues | 4,112 bytes | `APP/acm_bridge.c` | `g_host_to_target` + `g_target_to_host` |
| UAC jitter buffers | 3,072 bytes | `APP/uac_handler.c` | `g_h2t_buf` + `g_t2h_buf` |
| CH9329 parser buffer | 2,048 bytes | `APP/usbd_compostie_km.c` | `rx_buf` framed command assembly |
| `resp_queue` | 1,828 bytes | `APP/km_ring.c` | CH9329 response queue |
| `RingMemBLEBuf` + `RingMemUSBBuf` | 1,024 bytes | `APP/peripheral.c` | Raw BLE/USB ring buffers |
| USBFS endpoint buffers | 448 bytes visible as `USBFS_EP0_Buf`...`USBFS_EP6_Buf` | `APP/ch32v20x_usbfs_device.c` | Target USBFS software endpoint buffers |

Because the Mode 4 runtime image compiles Mode 1/2/3/4 support at once, SRAM includes buffers for UAC, ACM, ECM, BLE, HID queues, and command routing even when the currently active stored mode does not use every path.

## USB Packet Memory vs SRAM

There are two separate USB memory concepts in this project:

- Software buffers in SRAM, visible in the map file as `.bss` symbols such as `USBFS_EP5_Buf`, UAC FIFOs, ACM queues, and ECM queues.
- USB peripheral packet memory / PMA addresses configured through endpoint registers and helper calls such as `SetEPDblBuffAddr`, `UserToPMABufferCopy`, and `PMAToUserBufferCopy`.

PMA usage does not show up as normal `.bss` in the ELF size output. When changing endpoint packet sizes or double-buffering, check both the SRAM map and the USB endpoint/PMA layout.

## Build Config That Affects Memory

The remote build script sets key compile-time knobs in `tools/build_firmware.sh`:

```text
USB_WORK_MODE=USB_WORK_MODE_HID_ECM_BRIDGE (default in APP/include/usb_work_mode.h)
USB_WORK_MODE_RUNTIME_PROFILES=1 (default)
version=1
ENABLE_BLE=1
BLE_MEMHEAP_SIZE=10240
BLE_BUFF_MAX_LEN=516
BLE_TX_NUM_EVENT=5
KB_QUEUE_SIZE=512 (APP/include/km_ring.h)
```

Important memory levers:

- `USB_WORK_MODE=hid_ecm_bridge` enables the runtime profile image and compiles UAC, ACM, and ECM support together.
- `ENABLE_BLE=1` includes the BLE stack and `MEM_BUF` heap.
- `BLE_MEMHEAP_SIZE` directly sizes `MEM_BUF` in SRAM.
- UAC FIFO frame counts in `APP/uac_handler.c` directly scale audio SRAM use.
- ACM/ECM bridge queue depths and frame sizes directly scale bridge SRAM use.
- HID/response queue sizes in `APP/include/km_ring.h` are large static SRAM users.

## How To Check Usage

Build the runtime image:

```bash
bash tools/build_firmware.sh
```

Run the toolchain size command on the real build artifact:

```bash
riscv-none-embed-size --format=berkeley obj/openterface-km-firmware.elf
```

Expected output shape:

```text
   text    data     bss     dec     hex filename
214168    1024   49900  265092   40b84 obj/openterface-km-firmware.elf
```

Use the map file for exact symbols and section placement:

```bash
grep -E "Memory Configuration|_ebss|_heap_end|_eusrstack" -n obj/openterface-km-firmware.map
grep -E "kb_queue|MEM_BUF|g_h2t_buf|g_host_to_target" -n obj/openterface-km-firmware.map
```

Treat `obj/openterface-km-firmware.elf` and `obj/openterface-km-firmware.map` as the authoritative artifacts for `tools/build_firmware.sh` builds.

## Rules Of Thumb

- Watch SRAM more closely than flash. BLE + ECM/ACM/UAC + HID queues can push SRAM hard; keep a healthy free gap between `_end` and `_heap_end`.
- Any new large static array should be checked against `obj/openterface-km-firmware.map` after a clean build.
- If BLE advertising disappears (device stops showing up in scans), treat it as a potential SRAM pressure / stack headroom issue first.
- If a feature only needs one active runtime mode, prefer sharing buffers across modes instead of adding per-mode static buffers.
- Keep the last 4 KB flash page reserved for mode config unless the flash-write design is updated deliberately.
- Do not judge MCU usage from `.hex` file size; use ELF `size` and the map file.