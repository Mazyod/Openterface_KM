# Openterface KM Toolchain Setup Guide

This guide covers the complete setup and usage of the build and flash toolchain for Openterface KM firmware development.

## Table of Contents

1. [Toolchain Overview](#toolchain-overview)
2. [Installation](#installation)
3. [Environment Configuration](#environment-configuration)
4. [Building Firmware](#building-firmware)
5. [Flashing Firmware](#flashing-firmware)
6. [Testing](#testing)
7. [Troubleshooting](#troubleshooting)

---

## Toolchain Overview

The Openterface KM firmware uses the following toolchain:

| Component | Version | Purpose |
|-----------|---------|---------|
| **RISC-V GCC** | 8.2.0 (xPack) | Compiler for CH32V208 MCU |
| **OpenOCD** | 0.11.0+dev | Debug and flash tool |
| **wchisp** | Latest | WCH ISP flash tool |

### Supported Targets

- **kvmgo** - KVM Go (BOARD_VERSION=1)
- **minikvm** - Mini-KVM v1.2 (BOARD_VERSION=2)
- **keymod-plus** - KeyMod Plus with external oscillator
- **keymod-mini** - KeyMod Mini with internal RC oscillator

---

## Installation

### 1. Download MRS Toolchain

Download the MRS Toolchain from the official source or obtain it from your development team.

**File:** `MRS_Toolchain_MAC_V240.zip` (or similar)

### 2. Extract and Move Toolchain

Extract the toolchain to your home directory:

```bash
# Extract to Downloads (if zipped)
unzip MRS_Toolchain_MAC_V240.zip -d ~/

# Or move if already extracted
mv ~/Downloads/MRS_Toolchain_MAC_V240 ~/MRS_Toolchain_MAC_V240
```

### 3. Verify Toolchain Structure

The toolchain should have the following structure:

```
~/MRS_Toolchain_MAC_V240/
├── Toolchain/
│   ├── RISC-V Embedded GCC/          # GCC 8.2.0 (recommended)
│   │   └── bin/
│   │       ├── riscv-none-embed-gcc
│   │       ├── riscv-none-embed-objcopy
│   │       └── riscv-none-embed-size
│   ├── RISC-V Embedded GCC12/        # GCC 12 (alternative)
│   ├── RISC-V Embedded GCC15/        # GCC 15 (may have compatibility issues)
│   └── arm-none-eabi-gcc/            # ARM toolchain (not used)
└── OpenOCD/
    └── OpenOCD/
        └── bin/
            ├── openocd
            └── wch-riscv.cfg
```

### 4. Install wchisp

Install the WCH ISP tool via pip:

```bash
pip3 install wchisp
```

Or install via cargo (Rust):

```bash
cargo install wchisp
```

---

## Environment Configuration

### 1. Add Environment Variables to ~/.zshrc

```bash
# MRS Toolchain MAC V240 (WCH CH32V RISC-V GCC + OpenOCD)
export MRS_TOOLCHAIN_PATH="$HOME/MRS_Toolchain_MAC_V240"
export RISC_COMPILER_PATH="$MRS_TOOLCHAIN_PATH/Toolchain/RISC-V Embedded GCC/bin"
export PATH="$RISC_COMPILER_PATH:$MRS_TOOLCHAIN_PATH/OpenOCD/OpenOCD/bin:$PATH"

# WCH ISP Tool
export PATH="/Users/txa/project/WCHISPTool_CMD/macOS/bin:$PATH"

# wchisp (WCH ISP Tool in Rust)
export PATH="$HOME/.local/bin:$PATH"
```

### 2. Reload Shell Configuration

```bash
source ~/.zshrc
```

### 3. Verify Installation

Check that all tools are accessible:

```bash
# Verify RISC-V GCC
which riscv-none-embed-gcc
riscv-none-embed-gcc --version
# Expected: riscv-none-embed-gcc (xPack GNU RISC-V Embedded GCC x86_64) 8.2.0

# Verify OpenOCD
which openocd
openocd --version
# Expected: Open On-Chip Debugger 0.11.0+dev-snapshot

# Verify wchisp
which wchisp
wchisp --version
```

---

## Building Firmware

### 1. Navigate to Project Directory

```bash
cd /path/to/Openterface_KM
```

### 2. Build Commands

#### Build All Targets

```bash
./build.sh all
```

#### Build Specific Target

```bash
# Build KVM Go firmware
./build.sh kvmgo

# Build Mini-KVM firmware
./build.sh minikvm

# Build KeyMod Plus firmware
./build.sh keymod-plus

# Build KeyMod Mini firmware
./build.sh keymod-mini
```

#### Build Without BLE

```bash
# Build KVM Go without Bluetooth (saves ~14KB RAM)
./build.sh --no-ble kvmgo
```

#### Specify Compiler Path

```bash
# Use specific compiler path
./build.sh --compiler "/path/to/riscv-none-embed-gcc/bin" kvmgo
```

### 3. Build Output

After successful build, output files are in the `output/` directory:

```
output/
├── openterface-km-firmware-kvmgo.bin
├── openterface-km-firmware-kvmgo.hex
├── openterface-km-firmware-kvmgo.elf
└── openterface-km-firmware-kvmgo.map
```

### 4. Build Configuration

The build configuration is stored in `build/.build-config.stamp`:

```
BOARD_VERSION=1
USB_WORK_MODE=hid_acm
ENABLE_UAC=0
ENABLE_BLE=1
ENABLE_STYLUS=0
BLE_NAME=KVM-GO
ENABLE_OSCILLATOR=0
BLE_MEMHEAP_SIZE=6144
BLE_BUFF_MAX_LEN=64
BLE_TX_NUM_EVENT=1
```

---

## Flashing Firmware

### Method 1: Using wchisp (Recommended)

#### 1. Enter ISP Mode

To flash firmware, the device must be in ISP (In-System Programming) mode:

1. **Press and hold the BOOT button**
2. **Press and release the RESET button**
3. **Release the BOOT button**

The device should appear as a WCH ISP device:

```bash
wchisp probe
# Expected output: Found 1 USB device
#                  Device #0: CH32V208GBU6[0x8319]
```

#### 2. Flash Firmware

```bash
# Flash KVM Go firmware
wchisp flash output/openterface-km-firmware-kvmgo.bin

# Flash Mini-KVM firmware
wchisp flash output/openterface-km-firmware-minikvm.bin

# Flash KeyMod Plus firmware
wchisp flash output/openterface-km-firmware-keymod-plus.bin
```

#### 3. Verify Flash

After flashing, the device will automatically reset and enumerate as a USB serial device.

### Method 2: Using OpenOCD

OpenOCD can be used for debugging and flashing:

```bash
# Start OpenOCD with WCH RISC-V config
openocd -f ~/MRS_Toolchain_MAC_V240/OpenOCD/OpenOCD/bin/wch-riscv.cfg

# In another terminal, connect via telnet
telnet localhost 4444

# Flash firmware
> program openterface-km-firmware.elf verify reset exit
```

---

## Testing

### 1. Verify USB Enumeration

After flashing, check if the device is recognized:

```bash
# List USB serial devices
ls -la /dev/cu.usb*

# Expected output:
# crw-rw-rw-  1 root  wheel  ... /dev/cu.usbmodemXXXXXX
```

### 2. Test CH9329 Commands

The firmware implements the CH9329 protocol. Test with the provided script:

```bash
# Update the port in test_01_command.py if needed
python3 test_01_command.py
```

**Expected output:**
```
Opened /dev/cu.usbmodemXXXXXX at 9600 baud
Sending CMD_GET_INFO: 57ab00010003
Received response: 57ab008108010100060000000093
```

### 3. Test with Serial Terminal

#### Windows (Recommended for Serial Testing)

Use a serial terminal application like:
- PuTTY
- SecureCRT
- Windows Terminal with `mode` command

**Settings:**
- Baud rate: 9600
- Data bits: 8
- Parity: None
- Stop bits: 1
- Flow control: None

**Test Command:**
Send hex: `57 AB 00 01 00 03`
Expected response: `57 AB 00 81 08 01 01 00 06 00 00 00 00 93`

#### macOS/Linux

```bash
# Using screen
screen /dev/cu.usbmodemXXXXXX 9600

# Using minicom
minicom -D /dev/cu.usbmodemXXXXXX -b 9600
```

**Note:** macOS CDC ACM driver behavior may differ from Windows. If you experience issues with Python pyserial, try using Windows for serial testing.

---

## Troubleshooting

### Issue: Compiler Not Found

**Error:**
```
Error: riscv-none-embed-gcc compiler not found
```

**Solution:**
1. Verify the toolchain path in `~/.zshrc`
2. Reload shell: `source ~/.zshrc`
3. Check if the binary exists:
   ```bash
   ls -la ~/MRS_Toolchain_MAC_V240/Toolchain/RISC-V\ Embedded\ GCC/bin/riscv-none-embed-gcc
   ```

### Issue: wchisp Cannot Find Device

**Error:**
```
Error: No WCH ISP USB device found
```

**Solution:**
1. Ensure device is in ISP mode (BOOT button held during reset)
2. Check USB connection
3. Verify device permissions:
   ```bash
   lsusb | grep -i wch
   ```

### Issue: Build Fails with Linker Errors

**Error:**
```
undefined reference to `xxx'
```

**Solution:**
1. Clean build directory:
   ```bash
   cd build && make clean
   ```
2. Rebuild:
   ```bash
   ./build.sh kvmgo
   ```

### Issue: No Response from Serial Port

**Problem:** Device enumerates but doesn't respond to commands.

**Solutions:**

1. **Check baud rate:**
   - Default: 9600 baud
   - Some configurations may use 115200 baud

2. **Verify firmware version:**
   - Ensure you flashed the correct firmware for your board

3. **Try different OS:**
   - macOS CDC ACM driver may have compatibility issues
   - Test with Windows serial terminal

4. **Check USB cable:**
   - Use a data cable, not a charge-only cable
   - Try a different USB port

### Issue: OpenOCD Connection Fails

**Error:**
```
Error: open failed
```

**Solution:**
1. Verify device is connected
2. Check if another process is using the device:
   ```bash
   lsof | grep openocd
   ```
3. Ensure OpenOCD config matches your debug probe

---

## Quick Reference

### Build Commands

```bash
# Build KVM Go
./build.sh kvmgo

# Build all variants
./build.sh all

# Build without BLE
./build.sh --no-ble kvmgo
```

### Flash Commands

```bash
# Enter ISP mode, then:
wchisp flash output/openterface-km-firmware-kvmgo.bin
```

### Test Commands

```bash
# Probe device
wchisp probe

# Test 01 command
python3 test_01_command.py
```

### Useful Paths

```bash
# Toolchain location
~/MRS_Toolchain_MAC_V240/

# Compiler
~/MRS_Toolchain_MAC_V240/Toolchain/RISC-V\ Embedded\ GCC/bin/

# OpenOCD
~/MRS_Toolchain_MAC_V240/OpenOCD/OpenOCD/bin/

# Build output
output/openterface-km-firmware-*.bin
```

---

## Additional Resources

- [WCH CH32V208 Datasheet](https://www.wch-ic.com/products/CH32V208.html)
- [CH9329 Protocol Documentation](https://www.wch-ic.com/products/CH9329.html)
- [RISC-V GCC Toolchain](https://github.com/xpack-dev-tools/riscv-none-embed-gcc-xpack)
- [OpenOCD Documentation](https://openocd.org/doc/doxygen/html/index.html)

---

## Support

For issues or questions:
- Check the [Troubleshooting](#troubleshooting) section
- Review git commit history for recent changes
- Contact the development team

---

**Last Updated:** 2026-09-17
