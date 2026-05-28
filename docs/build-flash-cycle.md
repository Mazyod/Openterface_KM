# Auto Build & Flash Cycle

## Overview

Firmware must be **built on a Windows machine** (MounRiver Studio toolchain) and **flashed locally** (macOS/linux via `wchisp`). The local xpack GCC toolchain produces non-booting binaries.

## Prerequisites

### Remote Windows Machine

- SSH access: `admin@192.168.100.63`
- MSYS2 installed at `C:\msys64`
- MounRiver Studio toolchain (riscv-none-embed-gcc) available in MSYS2 PATH
- PowerShell `Compress-Archive` / `Expand-Archive` available

### Local macOS Machine

- `wchisp` installed (`~/.cargo/bin/wchisp`)
- `ssh` and `scp` available
- Target device connected via USB

## Build & Flash Steps

### 1. Clean old object files (local)

```bash
rm -f obj/APP/pwm_led.o obj/peripheral_main.o obj/RGB.o
```

### 2. Zip project source (local)

```bash
zip -r /tmp/openterface-km-build.zip \
  SRC/ Profile/ HAL/ LIB/ APP/ \
  tools/build_firmware.sh \
  tools/wchisp_c.c \
  HAL/Link.ld \
  -x "*/.DS_Store" -x "__MACOSX/*"
```

### 3. Transfer to Windows

```bash
scp /tmp/openterface-km-build.zip admin@192.168.100.63:/tmp/
```

### 4. Extract and build on Windows

```bash
ssh admin@192.168.100.63 'powershell -Command "Remove-Item -Recurse -Force C:\tmp\openterface-km-build -ErrorAction SilentlyContinue; Expand-Archive -Path C:\tmp\openterface-km-build.zip -DestinationPath C:\tmp\openterface-km-build -Force"'

ssh admin@192.168.100.63 'C:\msys64\usr\bin\bash.exe -l -c "cd /c/tmp/openterface-km-build && rm -rf obj && bash tools/build_firmware.sh"'
```

### 5. Fetch built binary (local)

```bash
scp admin@192.168.100.63:/tmp/openterface-km-build/obj/openterface-km-firmware.bin /tmp/openterface-km-firmware.bin
```

### 6. Flash locally

#### Enter ISP mode and flash via USB (preferred)

```bash
PORT="/dev/cu.usbmodem212101"

# Power off target
echo -n "57AB001A01001D" | xxd -r -p > "$PORT"
sleep 0.5

# Set BOOT pin HIGH (enter ISP mode on next boot)
echo -n "57AB001901011D" | xxd -r -p > "$PORT"
sleep 0.5

# Power on target (boots into ISP bootloader)
echo -n "57AB001A01011E" | xxd -r -p > "$PORT"
sleep 3

# Flash via USB (device shows up as VID:0x1A86 PID:0x55E0)
~/.cargo/bin/wchisp flash /tmp/openterface-km-firmware.bin

# Release BOOT pin AND power cycle to boot firmware
echo -n "57AB001901001C" | xxd -r -p > "$PORT"   # release BOOT
sleep 0.5
echo -n "57AB001A01001D" | xxd -r -p > "$PORT"   # power off
sleep 1
echo -n "57AB001A01011E" | xxd -r -p > "$PORT"   # power on (BOOT LOW)
sleep 5
```

**CRITICAL**: After flashing, you MUST release the BOOT pin AND power cycle. If the BOOT pin stays HIGH, the device will boot back into ISP mode and the firmware will never run. The wchisp auto-reset is not reliable — always do a manual power cycle with BOOT LOW.

#### Verify firmware is running

```bash
ioreg -p IOUSB -l 2>/dev/null | grep "Openterface Target"
# Should show idProduct 65024 (0xFE00 = KeyMod)
```

## Device Connection

### USB Topology

The Openterface setup has two USB connections:

1. **Control board → Mac**: Appears as two WCH USB Serial devices (`/dev/cu.usbmodem212101`, `/dev/cu.usbmodem212201`, VID:0x1A86 PID:0x9E6C) — this is the **control CH32** that manages the target's BOOT and power pins
2. **Target (CH32V208GBU6) → Mac**: Appears as "KeyMod" (VID:0x1A86 PID:0xFE00) in normal mode, or as WCH ISP bootloader (VID:0x1A86 PID:0x55E0) in ISP mode

### Flash Control Protocol

The target CH32V208GBU6's **BOOT pin** and **power pin** are controlled by the control CH32 via USB serial commands. No manual button-pressing is needed.

#### Command Format

```
57 AB 00 <cmd> <len> <data> <checksum>
```

| Control | Direction | Frame | Checksum |
|---------|-----------|-------|----------|
| Flash (BOOT) pin | Pull HIGH | `57 AB 00 19 01 01 1D` | 0x1D |
| Flash (BOOT) pin | Pull LOW | `57 AB 00 19 01 00 1C` | 0x1C |
| Power pin | Pull HIGH | `57 AB 00 1A 01 01 1E` | 0x1E |
| Power pin | Pull LOW | `57 AB 00 1A 01 00 1D` | 0x1D |

## Known Working Firmware

- `BLE_USB.hex` — known-good reference firmware (LED + BLE both work)
- `obj/openterface-km-firmware.bin` — built from this repo

## PC7 External LED Configuration (CONFIRMED WORKING)

### Hardware
- LED on **PC7 (GPIOC Pin 7)**, active-low (LOW = ON, HIGH = OFF)
- TIM3_CH2 with full remap: PC6=CH1, PC7=CH2, PC8=CH3, PC9=CH4

### Initialization Order (in `main()`)
1. `RGB_Init()` — initializes TIM3 (PSC=71, ARR=999, PWM1 mode, High polarity) and configures PC6/PC8/PC9
2. `PWM_LED_Init()` — adds TIM3_CH2 (PC7) without reinitializing the time base

**PWM_LED_Init() must be called AFTER RGB_Init()** because RGB_Init sets up the TIM3 FullRemap and time base. If PWM_LED_Init runs first, RGB_Init will overwrite its TIM3 config.

### Working PWM_LED_Init Implementation
```c
void PWM_LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    TIM_OCInitTypeDef TIM_OCInitStructure;

    /* Clocks (RGB_Init already enabled these) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);

    /* Full remap: TIM3_CH2 -> PC7 (same as RGB_Init) */
    GPIO_PinRemapConfig(GPIO_FullRemap_TIM3, ENABLE);

    /* PC7: alternate function push-pull for TIM3_CH2 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* TIM3 CH2: PWM mode 1, polarity HIGH, 100% brightness */
    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = 0;            /* 100% brightness (LED ON) */
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC2Init(TIM3, &TIM_OCInitStructure);

    TIM_OC2PreloadConfig(TIM3, TIM_OCPreload_Enable);
    g_brightness = 100;
}
```

### Brightness Control
With PWM1 + High polarity on an active-low LED:
- `CCR = 0`: counter always >= CCR -> pin LOW -> LED fully ON
- `CCR = 1000`: counter always < CCR -> pin HIGH -> LED fully OFF
- `CCR = (1000 - brightness * 1000 / 100)`: maps 0-100% brightness

### Build Script Config
- `CLK_OSC32K=1` (internal 32K RC oscillator for BLE)
- `ENABLE_BLE` defined
- `version=1` (kvm go mode)
- Links against `libwchble.a` in `LIB/`
- Toolchain: `C:/MounRiver/.../riscv-none-embed-gcc`
- Flags: `-march=rv32imacxw -mabi=ilp32 -Os`
