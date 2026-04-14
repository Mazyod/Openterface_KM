# Openterface KVM - Keyboard & Mouse Emulation Firmware

A firmware project running on a CH32V20x microcontroller that enables the Openterface KVM (KVMGo and MiniKVM v2) to control target devices by emulating keyboard and mouse inputs with dual wireless (BLE) and wired connectivity from the host computer. Supports CH9329 communication protocol for backward compatibility with the original MiniKVM.

## Project Description

**Openterface_KM** is the embedded firmware for a USB/BLE device that connects to the target machine. It receives input commands from the Openterface KVM controller (KVMGo or MiniKVM v2) and emulates them as keyboard and mouse HID devices on the target system. This firmware acts as the communication bridge between the host computer and the target computer, enabling seamless remote control via both USB wired and Bluetooth wireless connectivity.

### Key Features

- **Keyboard Emulation**: Emulates a USB HID keyboard with full support for standard key presses and special function keys
- **Mouse Emulation**: Supports both absolute and relative mouse positioning modes for versatile cursor control
- **CH9329 Protocol Support**: Implements the CH9329 communication protocol for reliable command reception and device communication
- **Dual Connectivity**:
  - **USB Connection**: Direct USB composite device with keyboard and mouse HID interfaces
  - **Bluetooth Low Energy (BLE)**: Wireless connectivity option for flexible deployment
  - **USB Ethernet (CDC-ECM)**: USB Ethernet dongle mode on the legacy USBD controller (separate USB port from HID). Includes CDC-ACM serial alongside ECM. Build with `ENABLE_ETH=1` — UAC is automatically disabled. See [Ethernet Support](#ethernet-support).
- **Hardware Features**:
  - Efficient input processing with ring buffer support for smooth keyboard and mouse handling
  - RGB LED status indication for connection and mode visualization
  - Temperature sensor support (DS18B20)
  - SD card detection and mode switching
  - USB debugging interface
  - RTC and power management with sleep mode support

## Source Folder Structure

```
├── APP/                          # Application layer code
│   ├── *.c/*.h                  # Main application files (keyboard, mouse, RGB, etc.)
│   ├── UART/                    # UART communication module
│   └── USBLIB/                  # USB library implementation
│       ├── CONFIG/              # USB configuration and descriptors
│       └── USB-Driver/          # USB driver implementation
│
├── HAL/                          # Hardware Abstraction Layer
│   ├── *.c/*.h                  # HAL implementations (LED, KEY, RTC, SLEEP, etc.)
│   └── Link.ld                  # Linker script for memory layout
│
├── SRC/                          # Core and peripheral library
│   ├── Core/                    # RISC-V core implementation
│   ├── Debug/                   # Debug utilities
│   ├── Peripheral/              # CH32V20x peripheral drivers
│   │   └── inc/                # CH32V20x header files (ADC, GPIO, UART, USB, etc.)
│   └── Startup/                 # Startup code for different chip variants
│
├── LIB/                          # BLE and library files
│   ├── wchble.h                # WCH BLE ROM interface
│   ├── wchble_rom.h            # BLE ROM implementation
│   └── ble_task_scheduler.S    # BLE task scheduling
│
├── Profile/                      # BLE GATT profiles
│   ├── gattprofile.c           # Generic GATT profile
│   ├── devinfoservice.c         # Device information service
│   └── ble_usb_service.c       # Custom BLE USB service
│
├── build/                        # Build output directory
│   ├── makefile                # Main build configuration
│   ├── sources.mk              # Source files list
│   └── objects.mk              # Object files mapping
│
└── BLE_USB.*                     # Project configuration files

```

### Key Files

| File/Folder | Purpose |
|-------------|---------|
| `APP/peripheral_main.c` | Main application entry point and main loop |
| `APP/keyboard_handler.c` | Keyboard HID input processing and emulation |
| `APP/mouse_handler.c` | Mouse HID input processing (absolute and relative modes) |
| `APP/usbd_composite_km.c` | USB composite device implementation (keyboard + mouse) |
| `APP/usbd_desc.c` | USB device descriptors and HID report descriptors |
| `Profile/gattprofile.c` | BLE GATT profile definitions for keyboard/mouse services |
| `Profile/ble_usb_service.c` | Custom BLE service for USB emulation data |
| `HAL/` | Hardware-specific implementations (LED, RTC, power management) |
| `SRC/Peripheral/` | CH32V20x microcontroller peripheral drivers |

### Input Modes

The firmware supports multiple input modes for mouse emulation:

- **Absolute Mouse Mode**: Reports absolute cursor position within a coordinate space (0-32767 for X and Y axes). Ideal for drawing and precision tasks.
- **Relative Mouse Mode**: Reports relative cursor movements (delta X and Y). Standard for typical mouse usage.
- **Keyboard Mode**: Full USB keyboard emulation with support for modifier keys and special functions.

## Communication Protocol

### CH9329 Protocol

This firmware implements the **CH9329 communication protocol**, which is compatible with the WCH CH9329 chipset used in the original Openterface MiniKVM. By emulating the CH9329 protocol, this firmware provides backward compatibility with the software of first generation MiniKVM, allowing seamless operation with the same command structure and interface that the original CH9329 hardware provided.

**Key Benefits**:
- Full backward compatibility with original Openterface MiniKVM systems
- Standardized command structure for keyboard and mouse control
- Built-in error checking and command validation
- Seamless integration with existing KVM infrastructure

## Prerequisites

Before compiling, ensure you have the following:

- **WCH MounRiver Studio** (Windows, recommended): includes the RISC-V toolchain and one-click flash support.
- **xpack riscv-none-elf-gcc** (macOS / Linux): see [macOS build guide](docs/build-on-mac.md).
- **GNU Make** (`brew install make` on macOS).
- **libusb + pkg-config** (`brew install libusb pkg-config` on macOS) for the bundled command-line flasher.
- **Git** (for cloning the repository).

> The build system uses the `riscv-none-embed-gcc` wrapper name.  
> On macOS the [xpack toolchain](docs/build-on-mac.md) provides wrapper scripts that translate the WCH-specific `-march=rv32imacxw` flag to the standard `-march=rv32imac_zicsr_zifencei` automatically.

## How to Compile

### Using WCH MounRiver Studio (Windows, recommended)

1. **Open the project**: `File` → `Open Projects from File System` → select the `Openterface_KM` directory.
2. **Build**: right-click the project → `Build Project`.  
   The binary is placed in `build/openterface-km-firmware.elf`.

### Command Line (macOS / Linux)

See [docs/build-on-mac.md](docs/build-on-mac.md) for full toolchain setup.

```bash
./tools/firmware.sh build
```

The helper script automatically uses `build/makefile` and the bundled WCH ISP flasher source in `tools/`.

Common commands:

```bash
# Build the default firmware
./tools/firmware.sh build

# Probe for the bootloader before flashing
./tools/firmware.sh probe

# Build and flash KeyMod BLE firmware
./tools/firmware.sh flash --clean --board-version 3 --enable-uac 0 --enable-eth 0 --enable-ble 1 --ble-name KeyMod-BLE

# Build and flash a BLE-only image with a custom BLE name
./tools/firmware.sh flash --clean --enable-uac 0 --enable-eth 0 --enable-ble 1 --ble-name Keymod-BLE
```

Direct `make` usage is still supported:

```bash
export PATH="$HOME/tools/xpack-riscv/wrappers:$PATH"
cd build
make clean && make all
make probe
make clean && make flash BOARD_VERSION=3 ENABLE_UAC=0 ENABLE_ETH=0 ENABLE_BLE=1 BLE_NAME=KeyMod-BLE
make clean && make flash ENABLE_UAC=0 ENABLE_ETH=0 ENABLE_BLE=1 BLE_NAME=Keymod-BLE
```

When switching feature flags or `BLE_NAME`, prefer a clean build for that first flash.
If you are flashing KeyMod hardware, use `BOARD_VERSION=3` for BLE-only firmware or `BOARD_VERSION=4` for the CDC-ECM variant. The default remains `BOARD_VERSION=1` (KVMGo).

Build output:

```
build/openterface-km-firmware.elf   ← flash this
build/openterface-km-firmware.bin   ← raw binary image used by the ISP flasher
build/openterface-km-firmware.hex   ← Intel HEX image
```

### Build Flags

All flags are set via `make` variables and default to **on**.  
Pass them on the command line to override, e.g. `make ENABLE_BLE=0`.

#### `ENABLE_ETH` — USB Ethernet (CDC-ECM)

```bash
make all ENABLE_ETH=1   # CDC-ECM Ethernet + CDC-ACM serial on the legacy USBD controller
```

| Value | Effect |
|---|---|
| `0` (default) | Standard CDC-ACM serial + UAC included |
| `1` | CDC-ECM Ethernet (IF0/IF1) + CDC-ACM serial (IF5/IF6) on the legacy USBD controller. **UAC is automatically forced off** to save RAM and free EP4/EP5. BLE is unaffected. |

When `ENABLE_ETH=1` the legacy USBD controller (separate physical USB port from HID) exposes:

| Endpoint | Direction | Function |
|---|---|---|
| EP1 IN | Interrupt | CDC-ECM NETWORK_CONNECTION notification |
| EP2 OUT | Bulk | Ethernet frames host → device (32 B/packet) |
| EP3 IN | Bulk | Ethernet frames device → host (32 B/packet) |
| EP6 IN | Interrupt | CDC-ACM serial notification |
| EP7 OUT | Bulk | Serial data host → device |
| EP7 IN | Bulk | Serial data device → host |

The HID, BLE, and (if enabled) UAC endpoints on the USBFS OTG controller are **not affected**.

> **Note**: The firmware provides only the USB transport layer (CDC-ECM bulk framing). A network stack (lwIP or similar) must be integrated separately to use the Ethernet interface.

#### `ENABLE_UAC` — USB Audio Class

```bash
make all ENABLE_UAC=0   # strip UAC, frees EP5 + EP6 on the USB controller
```

| Value | Effect |
|---|---|
| `1` (default) | UAC microphone/speaker interface included |
| `0` | UAC descriptors and endpoints removed; USB device re-enumerates without audio |

#### `ENABLE_BLE` — Bluetooth Low Energy

```bash
make all ENABLE_BLE=0   # strip BLE entirely, frees ~7 KB static RAM (BSS)
```

| Value | Effect |
|---|---|
| `1` (default) | BLE stack linked, BLE task runs alongside USB HID |
| `0` | BLE source files excluded, `-lwchble` not linked; FLASH drops from ~43% to ~8% |

When `ENABLE_BLE=1` the following BLE parameters can also be tuned:

| Variable | Default | Minimum | Description |
|---|---|---|---|
| `BLE_MEMHEAP_SIZE` | `7168` | `6144` | Bytes of static RAM given to the BLE stack heap |
| `BLE_BUFF_MAX_LEN` | `64` | `27` | Max HCI packet payload (ATT_MTU = value − 4) |
| `BLE_TX_NUM_EVENT` | `1` | `1` | Max TX packets per connection event |

Examples:

```bash
# Larger MTU for faster BLE throughput (costs more heap RAM)
make all BLE_MEMHEAP_SIZE=8192 BLE_BUFF_MAX_LEN=128

# Minimal BLE — keyboard/mouse only, minimum RAM
make all BLE_MEMHEAP_SIZE=7168 BLE_BUFF_MAX_LEN=64 BLE_TX_NUM_EVENT=1
```

#### Combining flags

```bash
# USB-only build (no BLE, no UAC) — smallest image, maximum free RAM
make all ENABLE_BLE=0 ENABLE_UAC=0

# Ethernet build — CDC-ECM + CDC-ACM serial, BLE still active, UAC auto-disabled
make all ENABLE_ETH=1

# Ethernet build, also disable BLE for minimum RAM
make all ENABLE_ETH=1 ENABLE_BLE=0
```

### Memory Usage Summary

| Build configuration | FLASH | Static RAM (BSS+DATA) |
|---|---|---|
| BLE=1, UAC=1 (default) | 42.65% | 38 KB |
| BLE=1, UAC=0 | ~42% | 38 KB |
| BLE=0, UAC=1 | 8.05% | 29 KB |
| BLE=0, UAC=0 | ~7.7% | 29 KB |
| ETH=1, BLE=1 (UAC auto-off) | ~48% | 37 KB |
| ETH=1, BLE=0 (UAC auto-off) | ~13% | 28 KB |

> **RAM note**: the linker script fills remaining RAM with the stack region, so the linker always reports 100%. The relevant numbers are the BSS+DATA figures in the table above. With BLE=1 default settings there is ~27 KB available for stack at runtime.

### Troubleshooting Build Issues

1. **Compiler not found**
   ```bash
   which riscv-none-embed-gcc
   ```
   If missing, follow [docs/build-on-mac.md](docs/build-on-mac.md).

2. **Permission errors in the build directory**
   ```bash
   chmod -R u+w build/
   ```

3. **Stale object files**
   ```bash
   make clean && make all
   ```

4. **`BLE_LibInit` fails at runtime** (BLE=1, device hangs on boot or no advertising)
   Increase the heap: `make all BLE_MEMHEAP_SIZE=8192`.

## How to Flash to Device

### Prerequisites

You’ll need the target device’s USB port connected to your computer 
(no external WCH-LinkE/LinkV programmer required). The flashing tool is WCHISPStudio

### Using WCHISPStudio

1. **Connect the device**:
   - Plug the device into your computer via the USB port
   - Ensure the device enters its built-in bootloader or USB-ISP mode (if required by your hardware design)

2. **Open WCHISPStudio**:
   - Launch the WCHISPStudio application (provided by WCH Nanjing Qinheng Microelectronics)
   - The tool should detect the target device connected via USB

3. **Flash the firmware**:
   - Load the binary file: `build/obj/BLE_USB.hex`
   - Ensure the correct MCU is selected: **CH32V208**
   - Click the **Download** or **Program** button
   - Wait for the operation to complete successfully
   - You should see a success message indicating the flash is complete

### Using WCH MounRiver Studio Built-in Flash Tool (Alternative)

If using WCH MounRiver Studio:

1. **Build the project** (as described in "How to Compile" section)
2. **Configure the debugger**:
   - Connect device via USB
   - Ensure the device is connected to the target
3. **Flash the firmware**:
   - Click the **Debug** or **Run** button in the toolbar
   - The firmware will be automatically compiled and flashed
   - Check the Console for completion status

### Verification

After flashing:

1. The device should enumerate as a USB device on your computer
2. Check system logs to confirm successful connection:
   - **Windows**: Device Manager → Ports (COM & LPT) or Human Interface Devices
   - **macOS**: `system_profiler SPUSBDataType`
   - **Linux**: `lsusb`

3. The RGB LED should indicate the device status:
   - Startup sequence animation
   - Steady light for normal operation
   - Different patterns for BLE/USB modes

## Ethernet Support

The CH32V208 has **two independent USB controllers**:

| Controller | Base address | IRQ | Current use |
|---|---|---|---|
| Legacy USB Device (USBD) | `0x40005C00` | IRQ 36 | CDC-ACM serial → **CDC-ECM + CDC-ACM** with `ENABLE_ETH=1` |
| USBFS OTG | `0x50000000` | IRQ 59 | HID keyboard + mouse (always) |

When `ENABLE_ETH=1`, the **legacy USBD controller** switches from CDC-ACM-only to a composite device with:
- **CDC-ECM** (IF0/IF1, EP1/EP2/EP3) — USB Ethernet framing
- **CDC-ACM** (IF5/IF6, EP6/EP7) — serial port retained alongside ECM

The HID controller (USBFS OTG) and BLE are completely unaffected. UAC is forced off because the UAC ping-pong buffers plus the 1514-byte ECM frame reassembly buffer would exceed available RAM.

### Dual USB port layout

```
Host PC
 │
 ├── USB port A (USBD, legacy)   ──► [ENABLE_ETH=0] CDC-ACM serial + UAC audio
 │                                   [ENABLE_ETH=1] CDC-ECM Ethernet + CDC-ACM serial
 │
 └── USB port B (USBFS OTG)      ──► HID keyboard + mouse (always)
                                     BLE → USBFS bridge (if ENABLE_BLE=1)
```

### Build

```bash
# CDC-ECM Ethernet + CDC-ACM serial, BLE still active
make all ENABLE_ETH=1

# Same without BLE (minimum RAM)
make all ENABLE_ETH=1 ENABLE_BLE=0
```

### CDC-ECM PMA endpoint layout (ENABLE_ETH=1)

```
EP0           Control                0x040 / 0x080  (64B each)
EP1 IN  INT   ECM notifications      0x0C0  (32B)
EP2 OUT BULK  Ethernet host→device   0x0E0  (32B, max packet)
EP3 IN  BULK  Ethernet device→host   0x100  (32B, max packet)
EP6 IN  INT   ACM serial notif.      0x1A0  (32B)
EP7 OUT BULK  Serial  host→device    0x1C0  (32B)
EP7 IN  BULK  Serial  device→host    0x1E0  (32B)
Total PMA: 512 B (exactly full)
```

Bulk max packet size is 32 bytes (constrained by PMA). Frame reassembly uses a 1514-byte buffer in RAM; large Ethernet frames are fragmented automatically.

### Application API

Override the weak callback in your application to receive frames, and call `ETH_TxFrame()` to transmit:

```c
/* Called from USB ISR when a complete Ethernet frame arrives from the host */
void ETH_OnRxFrame(const uint8_t *buf, uint16_t len)
{
    /* process buf[0..len-1] */
}

/* Transmit an Ethernet frame to the host (returns 0 if EP3 busy or link down) */
uint8_t ETH_TxFrame(const uint8_t *buf, uint16_t len);

/* Tell host link is up/down after SET_INTERFACE activates the data interface */
void ETH_SendNotification(uint8_t connected);
```

`g_eth_data_alt` is set to `1` by the USB stack when the host selects the active alternate setting on IF1 (CDC-ECM data interface). `ETH_TxFrame()` will return `0` until this happens.

> **Note**: the firmware provides only the USB transport layer. A network stack (lwIP or similar) must be integrated separately.

## Configuration

### Feature flags

See the [Build Flags](#build-flags) section above for `ENABLE_BLE` and `ENABLE_UAC`.

For compile-time BLE tuning that is not exposed as a `make` variable, edit `HAL/include/config.h`:

```c
#define HAL_SLEEP          FALSE   // Enable BLE sleep/low-power mode
#define TEM_SAMPLE         TRUE    // RF calibration via temperature sensor
#define BLE_CALIBRATION_ENABLE TRUE // Periodic BLE clock calibration (every 120 s)
#define PERIPHERAL_MAX_CONNECTION 1 // Max simultaneous BLE peripheral connections
#define CENTRAL_MAX_CONNECTION    0 // Central role disabled (saves heap RAM)
```

## Development Environment

This project uses:
- **IDE**: MCUEclipse or Eclipse CDT
- **Toolchain**: GNU MCU Eclipse RISC-V Embedded GCC
- **Target MCU**: WCH CH32V208 (RISC-V ISA)

## Integration with Openterface KVM

The Openterface KVM series (KVMGo and MiniKVM v2) is a KVM-over-USB controller that enables direct access to target computers. This firmware provides the device-side component that translates Openterface KVM controller input commands into HID keyboard and mouse events on the target system.

### How It Works

The firmware bridges the Openterface KVM controller with the target system:

1. **Receives Commands**: Accepts keyboard and mouse input commands from the Openterface KVM controller via USB or BLE
2. **Emulates HID Devices**: Presents itself as a standard USB keyboard and mouse to the target operating system
3. **Translates Input**: Converts command protocols into HID reports that the target OS understands
4. **Flexible Connectivity**: Supports both direct USB and wireless BLE connections(Only supports keyboard and mouse parts)

### System Architecture

```
Openterface KVM Controller (KVMGo / MiniKVM v2)
        ↓ 
    USB HID: Keyboard + Mouse
        ↓
    Target System (Windows/macOS/Linux)
```

This firmware runs on a device attached to the target machine, allowing the Openterface KVM controller to control the target system as if it were a local keyboard and mouse.

## Project Files Overview

- `.project`: Eclipse project descriptor
- `.cproject`: Eclipse C/C++ project configuration
- `BLE_USB.launch`: Eclipse debug launch configuration
- `BLE_USB.wvproj`: WCH IDE project file
- `makefile`: Build automation script

## Troubleshooting

### Common Issues

**Issue**: Device not recognized after flashing
- **Solution**: Check USB cable connection and verify the device powers on (check LED status)

**Issue**: Compilation fails with undefined references
- **Solution**: Ensure all submodules are initialized and all source files are listed in `build/sources.mk`

**Issue**: BLE not working
- **Solution**: Verify BLE configuration in `config.h` and check that the BLE library (ROM) is correctly linked

## Contributing

For bug reports and feature requests, please contact the maintainers or refer to the project's issue tracker.

## License

This project contains code from WCH (Nanjing Qinheng Microelectronics) for the CH32V20x peripheral libraries.
Please refer to the individual file headers for specific licensing information.

## Support

For questions and support:
- Check WCH's official documentation: http://www.wch.cn/
- Review CH32V20x datasheet for hardware details
- Consult the BLE profile implementations in the `Profile/` directory
