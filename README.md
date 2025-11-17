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
- **Hardware Features**:
  - Efficient input processing with ring buffer support for smooth keyboard and mouse handling
  - RGB LED status indication for connection and mode visualization
  - Temperature sensor support (DS18B20)
  - SD card detection and mode switching
  - UART debugging interface
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
- **Keyboard Mode**: Full USB/BLE keyboard emulation with support for modifier keys and special functions.

## Communication Protocol

### CH9329 Protocol

This firmware implements the **CH9329 communication protocol**, which is compatible with the WCH CH9329 chipset used in the original Openterface MiniKVM. By emulating the CH9329 protocol, this firmware provides backward compatibility with the software of first generation MiniKVM, allowing seamless operation with the same command structure and interface that the original CH9329 hardware provided.

**Key Benefits**:
- Full backward compatibility with original Openterface MiniKVM systems
- Standardized command structure for keyboard and mouse control
- Built-in error checking and command validation
- Seamless integration with existing KVM infrastructure

## Prerequisites

Before compiling and flashing, ensure you have the following tools installed:

- **WCH MounRiver Studio**: Integrated development environment for CH32V series microcontrollers
- **WCH Flash Uploader**: Tool for flashing firmware to the device
- **Git**: Version control (optional, for cloning)

### Installation

#### Windows (Recommended)

1. **Download and Install WCH MounRiver Studio**:
   - Visit [WCH's official website](http://www.wch.cn/)
   - Download MounRiver Studio for Windows
   - Follow the installation instructions
   - The toolchain and build tools are included with MounRiver Studio

2. **Download and Install WCH Flash Uploader**:
   - Download from [WCH's official website](http://www.wch.cn/)
   - Follow the installation instructions
   - This tool will be used to flash the compiled firmware to the device

3. **Verify Installation**:
   - Open MounRiver Studio
   - The project should be recognized and ready to build

#### macOS/Linux (Alternative)

For development on macOS or Linux systems:

```bash
# macOS - Install RISC-V toolchain via Homebrew
brew tap xpack-dev-tools/riscv-embedded-gcc
brew install riscv-embedded-gcc
brew install make

# Linux (Ubuntu/Debian)
sudo apt-get install build-essential
sudo apt-get install gcc-riscv64-unknown-elf
```

**Note**: WCH MounRiver Studio is the recommended development environment. Command-line compilation is supported but may require additional configuration.

## How to Compile

### Using WCH MounRiver Studio (Recommended)

1. **Open the project**:
   - Launch WCH MounRiver Studio
   - Click `File` → `Open Projects from File System`
   - Navigate to the `Openterface_KM` directory and select it
   - The project will be imported automatically

2. **Build the project**:
   - Right-click on the project in the Project Explorer
   - Select `Build Project` or use the keyboard shortcut
   - Alternatively, click the Build button in the toolbar
   - The build progress will be shown in the Console panel

3. **View build output**:
   - Check the Console tab for build messages
   - If successful, you should see "Build Finished" message

### Quick Build (Command Line)

For command-line compilation:

```bash
cd /path/to/Openterface_KM
cd build
make clean
make
```

### Build Output

After successful compilation, the executable will be generated as:
```
build/obj/BLE_USB.elf
```

An additional binary file may also be created:
```
build/obj/BLE_USB.bin
```

### Build Targets

```bash
# Clean build artifacts
make clean

# Build the project
make

# Rebuild everything
make clean && make
```

### Troubleshooting Build Issues

1. **Compiler not found**: Ensure RISC-V cross compiler is in your PATH
   ```bash
   which riscv-none-embed-gcc
   ```

2. **Permission errors**: Make sure you have write permissions in the build directory
   ```bash
   chmod +x build/
   ```

3. **Stale object files**: Clean and rebuild
   ```bash
   make clean && make
   ```

## How to Flash to Device

### Prerequisites

You'll need a WCH-LinkE or WCH-LinkV programmer/debugger connected to your system.

### Using WCH Flash Uploader (Recommended)

1. **Connect the programmer**:
   - Connect WCH-LinkE/LinkV to your computer via USB
   - Connect the debug interface of the programmer to the CH32V20x device

2. **Open WCH Flash Uploader**:
   - Launch the WCH Flash Uploader application (installed with WCH tools)
   - The programmer should be automatically detected

3. **Flash the firmware**:
   - Load the binary file: `build/obj/BLE_USB.bin`
   - Ensure the correct MCU is selected: **CH32V208**
   - Set the flash address if necessary (typically 0x00000000 for main program)
   - Click the **Download** or **Program** button
   - Wait for the operation to complete successfully
   - You should see a success message indicating the flash is complete

### Using WCH MounRiver Studio Built-in Flash Tool (Alternative)

If using WCH MounRiver Studio:

1. **Build the project** (as described in "How to Compile" section)
2. **Configure the debugger**:
   - Connect WCH-LinkE/LinkV via USB
   - Ensure the device is connected to the target
3. **Flash the firmware**:
   - Click the **Debug** or **Run** button in the toolbar
   - Select the WCH-Link programmer when prompted
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

## Configuration

### Build Configuration

Key configuration files:

- `HAL/include/config.h`: Hardware and BLE configuration options
- `.cproject`: Eclipse/MCUEclipse project settings
- `build/sources.mk`: Source files inclusion

### Common Configuration Options (in `config.h`)

```c
#define BLE_MAC                     FALSE    // Use chip MAC or custom
#define HAL_SLEEP                   FALSE    // Enable sleep mode
#define TEM_SAMPLE                  TRUE     // Temperature sampling
#define BLE_CALIBRATION_ENABLE      TRUE     // BLE calibration
#define PERIPHERAL_MAX_CONNECTION   1        // Max BLE connections
```

## Development Environment

This project uses:
- **IDE**: MCUEclipse or Eclipse CDT
- **Toolchain**: GNU MCU Eclipse RISC-V Embedded GCC
- **Target MCU**: WCH CH32V208 (RISC-V ISA)

## Integration with Openterface KVM

The Openterface KVM series (KVMGo and MiniKVM v2) is a KVM-over-IP controller that enables remote access to target computers. This firmware provides the device-side component that translates Openterface KVM controller input commands into HID keyboard and mouse events on the target system.

### How It Works

The firmware bridges the Openterface KVM controller with the target system:

1. **Receives Commands**: Accepts keyboard and mouse input commands from the Openterface KVM controller via USB or BLE
2. **Emulates HID Devices**: Presents itself as a standard USB keyboard and mouse to the target operating system
3. **Translates Input**: Converts command protocols into HID reports that the target OS understands
4. **Flexible Connectivity**: Supports both direct USB and wireless BLE connections

### System Architecture

```
Openterface KVM Controller (KVMGo / MiniKVM v2)
        ↓
    USB or BLE
        ↓
    Openterface_KM (CH32V20x Microcontroller)
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
