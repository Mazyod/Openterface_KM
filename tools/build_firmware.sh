#!/usr/bin/env bash
# Standalone firmware build script for Openterface KM
# Uses the MounRiver Studio bundled toolchain
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/firmware_build"

TOOLCHAIN="C:/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC/bin"
PREFIX="$TOOLCHAIN/riscv-none-embed"

CC="$PREFIX-gcc"
OBJCOPY="$PREFIX-objcopy"
OBJDUMP="$PREFIX-objdump"
SIZE="$PREFIX-size"

# Build configuration
BOARD_VERSION=1
ENABLE_UAC=1
ENABLE_BLE=1
BLE_NAME="KVM-GO"

# MCU flags
MARCH="-march=rv32imacxw"
MABI="-mabi=ilp32"
MCFLAGS="$MARCH $MABI -mcmodel=medany -msmall-data-limit=8 -mno-save-restore"

# Source files
C_SOURCES=$(find "$REPO_ROOT/APP" "$REPO_ROOT/HAL" "$REPO_ROOT/Profile" "$REPO_ROOT/SRC/Core" "$REPO_ROOT/SRC/Debug" "$REPO_ROOT/SRC/Peripheral/src" -name "*.c" 2>/dev/null | grep -v "APP/USBLIB/USB-Driver/CH32_TEMP.c" | grep -v "APP/USBLIB/USB-Driver/src/CH32_TEMP.c")

# Use the D8W startup file (CH32V203 with BLE)
ASM_SOURCES="$REPO_ROOT/SRC/Startup/startup_ch32v20x_D8W.S"

# Object files
C_OBJECTS=$(echo "$C_SOURCES" | sed "s|$REPO_ROOT/|$BUILD_DIR/|; s|\.c$|.o|")
ASM_OBJECT=$(echo "$ASM_SOURCES" | sed "s|$REPO_ROOT/|$BUILD_DIR/|; s|\.S$|.o|")

# Include paths
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

CFLAGS="$MCFLAGS -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g"
CFLAGS="$CFLAGS -include stdint.h -include stddef.h"
CFLAGS="$CFLAGS -Dversion=$BOARD_VERSION"
CFLAGS="$CFLAGS -DUPSTREAM_USB_MODE=1"
CFLAGS="$CFLAGS -DUSB_ETH_MAC=0x024F464B4D01ULL"
CFLAGS="$CFLAGS -DUSB_ETH_MAX_SEGMENT_SIZE=1514"
CFLAGS="$CFLAGS -DCH32V20x_D8W"
CFLAGS="$CFLAGS -DBLE_BUFF_MAX_LEN=516"
CFLAGS="$CFLAGS -DBLE_MEMHEAP_SIZE=10240"
CFLAGS="$CFLAGS -DBLE_TX_NUM_EVENT=5"
CFLAGS="$CFLAGS -DCLK_OSC32K=0"

if [ "$ENABLE_UAC" = "1" ]; then
  CFLAGS="$CFLAGS -DENABLE_UAC"
fi
if [ "$ENABLE_BLE" = "1" ]; then
  CFLAGS="$CFLAGS -DENABLE_BLE"
  CFLAGS="$CFLAGS -DCENTRAL_MAX_CONNECTION=0"
fi

LINKER_SCRIPT="$REPO_ROOT/HAL/Link.ld"
OUTPUT="$BUILD_DIR/openterface-km-firmware"

mkdir -p "$BUILD_DIR"

echo "=== Compiling C sources ==="
for src in $C_SOURCES; do
  obj=$(echo "$src" | sed "s|$REPO_ROOT/|$BUILD_DIR/|; s|\.c$|.o|")
  mkdir -p "$(dirname "$obj")"
  echo "  $src"
  "$CC" $CFLAGS "${INCLUDES[@]}" -c "$src" -o "$obj"
done

echo "=== Compiling assembly startup ==="
mkdir -p "$(dirname "$ASM_OBJECT")"
"$CC" $MCFLAGS "${INCLUDES[@]}" -c "$ASM_SOURCES" -o "$ASM_OBJECT"

echo "=== Linking ==="
ALL_OBJECTS="$ASM_OBJECT $C_OBJECTS"
"$CC" $MCFLAGS -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g \
  "${INCLUDES[@]}" \
  -T "$LINKER_SCRIPT" -nostartfiles \
  -Xlinker --gc-sections -L"$REPO_ROOT/" -L"$REPO_ROOT/LIB" \
  -Xlinker --print-memory-usage \
  -Wl,-Map,"$OUTPUT.map" \
  --specs=nano.specs --specs=nosys.specs \
  -o "$OUTPUT.elf" $ALL_OBJECTS -lwchble -lm

echo "=== Creating HEX ==="
"$OBJCOPY" -O ihex "$OUTPUT.elf" "$OUTPUT.hex"

echo "=== Creating BIN ==="
"$OBJCOPY" -O binary "$OUTPUT.elf" "$OUTPUT.bin"

echo "=== Size report ==="
"$SIZE" --format=berkeley "$OUTPUT.elf"

echo ""
echo "Build complete: $OUTPUT.hex"
echo "Binary: $OUTPUT.bin"
