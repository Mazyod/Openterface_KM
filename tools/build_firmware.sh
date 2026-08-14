#!/usr/bin/env bash
# Standalone firmware build script for Openterface KM
# Uses the MounRiver Studio bundled toolchain
# Source order and flags match MRS obj/ subdir.mk exactly.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/obj"

# Toolchain selection:
# - On macOS/Linux, prefer `riscv-none-embed-*` from PATH (this repo often
#   provides wrapper scripts that map to an installed xPack toolchain).
# - On Windows, fall back to the MounRiver Studio bundled toolchain.
if command -v riscv-none-embed-gcc >/dev/null 2>&1; then
  CC="riscv-none-embed-gcc"
  OBJCOPY="riscv-none-embed-objcopy"
  OBJDUMP="riscv-none-embed-objdump"
  SIZE="riscv-none-embed-size"
else
  TOOLCHAIN="C:/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC/bin"
  PREFIX="$TOOLCHAIN/riscv-none-embed"
  CC="$PREFIX-gcc"
  OBJCOPY="$PREFIX-objcopy"
  OBJDUMP="$PREFIX-objdump"
  SIZE="$PREFIX-size"
fi

# MCU flags
MARCH="-march=rv32imacxw"
MABI="-mabi=ilp32"
MCFLAGS="$MARCH $MABI -mcmodel=medany -msmall-data-limit=8 -mno-save-restore"

# Source files — exact order matching MounRiver Studio obj/ subdir.mk link order.
# MRS map file link order (from obj/BLE_USB.map):
#   tools/wchisp_c, startup, SRC/Peripheral(20), SRC/Debug, SRC/Core,
#   Profile(3), HAL(MCU,RTC,SLEEP), APP/USBLIB/USB-Driver/src(6),
#   APP/USBLIB/CONFIG(6), APP/UART, APP(21).
# MRS does NOT compile HAL/KEY.c or HAL/LED.c.
C_SOURCES=(
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_adc.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_bkp.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_can.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_crc.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_dbgmcu.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_dma.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_exti.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_flash.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_gpio.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_i2c.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_iwdg.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_misc.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_pwr.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_rcc.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_rtc.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_spi.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_tim.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_usart.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_wwdg.c"
  "$REPO_ROOT/SRC/Debug/debug.c"
  "$REPO_ROOT/SRC/Core/core_riscv.c"
  "$REPO_ROOT/Profile/ble_usb_service.c"
  "$REPO_ROOT/Profile/devinfoservice.c"
  "$REPO_ROOT/Profile/gattprofile.c"
  "$REPO_ROOT/HAL/MCU.c"
  "$REPO_ROOT/HAL/RTC.c"
  "$REPO_ROOT/HAL/SLEEP.c"
  "$REPO_ROOT/APP/USBLIB/USB-Driver/src/usb_core.c"
  "$REPO_ROOT/APP/USBLIB/USB-Driver/src/usb_init.c"
  "$REPO_ROOT/APP/USBLIB/USB-Driver/src/usb_int.c"
  "$REPO_ROOT/APP/USBLIB/USB-Driver/src/usb_mem.c"
  "$REPO_ROOT/APP/USBLIB/USB-Driver/src/usb_regs.c"
  "$REPO_ROOT/APP/USBLIB/USB-Driver/src/usb_sil.c"
  "$REPO_ROOT/APP/USBLIB/CONFIG/hw_config.c"
  "$REPO_ROOT/APP/USBLIB/CONFIG/usb_desc.c"
  "$REPO_ROOT/APP/USBLIB/CONFIG/usb_endp.c"
  "$REPO_ROOT/APP/USBLIB/CONFIG/usb_istr.c"
  "$REPO_ROOT/APP/USBLIB/CONFIG/usb_prop.c"
  "$REPO_ROOT/APP/USBLIB/CONFIG/usb_pwr.c"
  "$REPO_ROOT/APP/UART/UART.c"
  "$REPO_ROOT/APP/CH32_TEMP.c"
  "$REPO_ROOT/APP/DS18B20.c"
  "$REPO_ROOT/APP/RGB.c"
  "$REPO_ROOT/APP/RingMem.c"
  "$REPO_ROOT/APP/acm_bridge.c"
  "$REPO_ROOT/APP/ble_eth_protocol.c"
  "$REPO_ROOT/APP/ch32v20x_it.c"
  "$REPO_ROOT/APP/ch32v20x_usbfs_device.c"
  "$REPO_ROOT/APP/ecm_bridge.c"
  "$REPO_ROOT/APP/iwdg_auto.c"
  "$REPO_ROOT/APP/keyboard_handler.c"
  "$REPO_ROOT/APP/km_ring.c"
  "$REPO_ROOT/APP/mouse_handler.c"
  "$REPO_ROOT/APP/peripheral.c"
  "$REPO_ROOT/APP/peripheral_main.c"
  "$REPO_ROOT/APP/pwm_led.c"
  "$REPO_ROOT/APP/sd_switch.c"
  "$REPO_ROOT/APP/system_ch32v20x.c"
  "$REPO_ROOT/APP/uac_handler.c"
  "$REPO_ROOT/APP/usb_mode_config.c"
  "$REPO_ROOT/APP/default_target_config.c"
  "$REPO_ROOT/APP/usbd_compostie_km.c"
  "$REPO_ROOT/APP/usbd_desc.c"
)

ASM_SOURCES="$REPO_ROOT/SRC/Startup/startup_ch32v20x_D8W.S"

# BLE task scheduler assembly (MRS compiles from LIB/subdir.mk)
BLE_TASK_SRC="$REPO_ROOT/LIB/ble_task_scheduler.S"
BLE_TASK_OBJ="$BUILD_DIR/LIB/ble_task_scheduler.o"

# tools/wchisp_c.c (guarded by #ifndef __riscv, compiles to empty sections)
TOOLS_SRC="$REPO_ROOT/tools/wchisp_c.c"
TOOLS_OBJ="$BUILD_DIR/tools/wchisp_c.o"

# Object files (in same order as sources)
C_OBJECTS=()
for src in "${C_SOURCES[@]}"; do
  obj="${src#$REPO_ROOT/}"
  obj="${obj%.c}.o"
  obj="$BUILD_DIR/$obj"
  C_OBJECTS+=("$obj")
done
ASM_OBJECT="$BUILD_DIR/${ASM_SOURCES#$REPO_ROOT/}"
ASM_OBJECT="${ASM_OBJECT%.S}.o"

# Include paths — same as MRS
INCLUDES=(
  "-I$REPO_ROOT/SRC/Core"
  "-I$REPO_ROOT/SRC/Startup"
  "-I$REPO_ROOT/SRC/Debug"
  "-I$REPO_ROOT/SRC/Peripheral/inc"
  "-I$REPO_ROOT/APP/include"
  "-I$REPO_ROOT/Profile/include"
  "-I$REPO_ROOT/HAL/include"
  "-I$REPO_ROOT/LIB"
  "-I$REPO_ROOT/APP/UART"
  "-I$REPO_ROOT/APP/USBLIB/CONFIG"
  "-I$REPO_ROOT/APP/USBLIB/USB-Driver/inc"
)

# Compiler flags — match MRS obj/ subdir.mk compile command exactly
CFLAGS="$MCFLAGS -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g"
CFLAGS="$CFLAGS -include stdint.h -include stddef.h"
CFLAGS="$CFLAGS -std=gnu99"
CFLAGS="$CFLAGS -DBLE_BUFF_MAX_LEN=516"
CFLAGS="$CFLAGS -DCH32V20x_D8W"
CFLAGS="$CFLAGS -DBLE_MEMHEAP_SIZE=10240"
CFLAGS="$CFLAGS -DBLE_TX_NUM_EVENT=5"
# 0 = external 32K (32768Hz) — needs physical crystal
# 1 = internal 32000Hz RC
# 2 = internal 32768Hz RC
# ENABLE_OSCILLATOR=1 selects external crystal (CLK_OSC32K=0).
# ENABLE_OSCILLATOR=0 (default) uses internal RC (CLK_OSC32K=1).
ENABLE_OSCILLATOR="${ENABLE_OSCILLATOR:-0}"
if [[ "$ENABLE_OSCILLATOR" == "1" ]]; then
  CFLAGS="$CFLAGS -DCLK_OSC32K=0"
else
  CFLAGS="$CFLAGS -DCLK_OSC32K=1"
fi

# Enable BLE, board version and BLE name from env (override at call time).
# Defaults: BOARD_VERSION=1 (KVMGo), ENABLE_BLE=1, ENABLE_UAC=0,
#           USB_WORK_MODE=hid_acm_ecm (CDC COM + CDC ECM for all boards).
# BLE_NAME auto-detected from BOARD_VERSION unless explicitly set.
BOARD_VERSION="${BOARD_VERSION:-1}"
ENABLE_BLE="${ENABLE_BLE:-1}"
ENABLE_UAC="${ENABLE_UAC:-0}"
USB_WORK_MODE="${USB_WORK_MODE:-hid_acm_ecm}"

if [[ -z "${BLE_NAME:-}" ]]; then
  case "$BOARD_VERSION" in
    1) BLE_NAME="KVM-GO" ;;
    2) BLE_NAME="Mini-KVM" ;;
    3) BLE_NAME="KeyMod" ;;
    *) BLE_NAME="KeyMod" ;;
  esac
fi

# Map USB_WORK_MODE name to numeric ID (must match build/makefile)
case "$USB_WORK_MODE" in
  hid_acm)        USB_WORK_MODE_ID=1 ;;
  hid_uac_acm)    USB_WORK_MODE_ID=2 ;;
  hid_acm_bridge) USB_WORK_MODE_ID=3 ;;
  hid_ecm_bridge) USB_WORK_MODE_ID=4 ;;
  hid_uac_ecm)    USB_WORK_MODE_ID=5 ;;
  hid_acm_ecm)    USB_WORK_MODE_ID=6 ;;
  *) echo "ERROR: unsupported USB_WORK_MODE '$USB_WORK_MODE'" >&2; exit 1 ;;
esac

if [[ "$ENABLE_BLE" == "1" ]]; then
  CFLAGS="$CFLAGS -DENABLE_BLE"
  CFLAGS="$CFLAGS -DBLE_DEVICE_NAME=\"$BLE_NAME\""
  echo "BLE_DEVICE_NAME=$BLE_NAME"
fi
if [[ "$ENABLE_UAC" == "1" ]]; then
  CFLAGS="$CFLAGS -DENABLE_UAC"
fi
CFLAGS="$CFLAGS -Dversion=$BOARD_VERSION"
CFLAGS="$CFLAGS -DUSB_WORK_MODE=$USB_WORK_MODE_ID"
CFLAGS="$CFLAGS -DUSB_WORK_MODE_RUNTIME_PROFILES=0"
echo "USB_WORK_MODE=$USB_WORK_MODE (ID=$USB_WORK_MODE_ID)"

LINKER_SCRIPT="$REPO_ROOT/HAL/Link.ld"
OUTPUT="$BUILD_DIR/openterface-km-firmware"

mkdir -p "$BUILD_DIR"

echo "=== Compiling C sources ==="
for i in "${!C_SOURCES[@]}"; do
  src="${C_SOURCES[$i]}"
  obj="${C_OBJECTS[$i]}"
  mkdir -p "$(dirname "$obj")"
  echo "  $src"
  "$CC" $CFLAGS "${INCLUDES[@]}" -c "$src" -o "$obj"
done

echo "=== Compiling BLE task scheduler ==="
mkdir -p "$(dirname "$BLE_TASK_OBJ")"
"$CC" $MCFLAGS "${INCLUDES[@]}" -x assembler-with-cpp -c "$BLE_TASK_SRC" -o "$BLE_TASK_OBJ"

echo "=== Compiling assembly startup ==="
mkdir -p "$(dirname "$ASM_OBJECT")"
"$CC" $MCFLAGS "${INCLUDES[@]}" -x assembler-with-cpp -c "$ASM_SOURCES" -o "$ASM_OBJECT"

echo "=== Compiling tools (empty for riscv) ==="
mkdir -p "$(dirname "$TOOLS_OBJ")"
"$CC" $CFLAGS "${INCLUDES[@]}" -c "$TOOLS_SRC" -o "$TOOLS_OBJ"

echo "=== Linking ==="
# MRS link order: tools/wchisp_c (empty on riscv), startup, SRC/Peripheral,
# SRC/Debug, SRC/Core, Profile, LIB/ble_task_scheduler, HAL, APP/USBLIB,
# APP/UART, APP.
PROFILE_END=25  # C_OBJECTS[0..24] = SRC/Peripheral(20) + Debug + Core + Profile(3)

ALL_OBJECTS=(
  "$TOOLS_OBJ"
  "$ASM_OBJECT"
  "${C_OBJECTS[@]:0:$PROFILE_END}"   # SRC/Peripheral + Debug + Core + Profile
  "$BLE_TASK_OBJ"                      # LIB/ble_task_scheduler (between Profile and HAL)
  "${C_OBJECTS[@]:$PROFILE_END}"       # HAL + USBLIB + UART + APP
)
"$CC" $MCFLAGS -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g \
  "${INCLUDES[@]}" \
  -T "$LINKER_SCRIPT" -nostartfiles \
  -Xlinker --gc-sections -L"$REPO_ROOT/" -L"$REPO_ROOT/LIB" \
  -Xlinker --print-memory-usage \
  -Wl,-Map,"$OUTPUT.map" \
  --specs=nano.specs --specs=nosys.specs \
  -o "$OUTPUT.elf" "${ALL_OBJECTS[@]}" -lwchble -lm

echo "=== Creating HEX ==="
"$OBJCOPY" -O ihex "$OUTPUT.elf" "$OUTPUT.hex"

echo "=== Creating BIN ==="
"$OBJCOPY" -O binary "$OUTPUT.elf" "$OUTPUT.bin"

echo "=== Size report ==="
"$SIZE" --format=berkeley "$OUTPUT.elf"

echo ""
echo "Build complete: $OUTPUT.hex"
echo "Binary: $OUTPUT.bin"
