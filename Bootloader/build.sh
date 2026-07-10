#!/usr/bin/env bash
# Build bootloader for Openterface KM (CH32V208)
# USB bulk transport on EP2 for firmware update.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/Bootloader/build"

if command -v riscv-none-embed-gcc >/dev/null 2>&1; then
  CC="riscv-none-embed-gcc"
  OBJCOPY="riscv-none-embed-objcopy"
  SIZE="riscv-none-embed-size"
else
  TOOLCHAIN="C:/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC/bin"
  PREFIX="$TOOLCHAIN/riscv-none-embed"
  CC="$PREFIX-gcc"
  OBJCOPY="$PREFIX-objcopy"
  SIZE="$PREFIX-size"
fi

MARCH="-march=rv32imaczicsr"
MABI="-mabi=ilp32"
MCFLAGS="$MARCH $MABI -mcmodel=medany -msmall-data-limit=8 -mno-save-restore"
CFLAGS="$MCFLAGS -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g"
CFLAGS="$CFLAGS -std=gnu99 -DCH32V20x_D8W"

INCLUDES=(
  "-I/tmp/riscv32-include"
  "-I$REPO_ROOT/SRC/Core"
  "-I$REPO_ROOT/SRC/Startup"
  "-I$REPO_ROOT/SRC/Debug"
  "-I$REPO_ROOT/SRC/Peripheral/inc"
  "-I$REPO_ROOT/APP/include"
  "-I$REPO_ROOT/Bootloader"
)

# SDK sources we need (minimal subset for flash, USB, BKP, IWDG, RCC, PWR, GPIO, core)
SDK_SOURCES=(
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_flash.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_bkp.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_rcc.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_pwr.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_gpio.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_iwdg.c"
  "$REPO_ROOT/SRC/Peripheral/src/ch32v20x_misc.c"
  "$REPO_ROOT/SRC/Core/core_riscv.c"
  "$REPO_ROOT/SRC/Debug/debug.c"
)

# Bootloader sources
BL_SOURCES=(
  "$REPO_ROOT/Bootloader/main.c"
  "$REPO_ROOT/Bootloader/bootloader.c"
  "$REPO_ROOT/Bootloader/protocol.c"
  "$REPO_ROOT/Bootloader/usb.c"
  "$REPO_ROOT/Bootloader/libc_minimal.c"
)

# We also need system_ch32v20x.c for clock setup
SYSTEM_SRC="$REPO_ROOT/APP/system_ch32v20x.c"

# Startup assembly
ASM_SRC="$REPO_ROOT/Bootloader/startup.S"

LINKER_SCRIPT="$REPO_ROOT/Bootloader/Link.ld"
OUTPUT="$BUILD_DIR/bootloader"

mkdir -p "$BUILD_DIR"

ALL_C_SOURCES=("${SDK_SOURCES[@]}" "${BL_SOURCES[@]}" "$SYSTEM_SRC")

echo "=== Compiling bootloader ==="
OBJECTS=()
for src in "${ALL_C_SOURCES[@]}"; do
  obj_name=$(basename "$src" .c)
  obj="$BUILD_DIR/${obj_name}.o"
  echo "  $src"
  "$CC" $CFLAGS "${INCLUDES[@]}" -c "$src" -o "$obj"
  OBJECTS+=("$obj")
done

echo "=== Compiling startup ==="
ASM_OBJ="$BUILD_DIR/startup.o"
"$CC" $MCFLAGS "${INCLUDES[@]}" -x assembler-with-cpp -c "$ASM_SRC" -o "$ASM_OBJ"
OBJECTS+=("$ASM_OBJ")

echo "=== Linking ==="
"$CC" $MCFLAGS -Os -ffunction-sections -fdata-sections \
  "${INCLUDES[@]}" \
  -T "$LINKER_SCRIPT" -nostartfiles -nostdlib \
  -Xlinker --gc-sections \
  -o "$OUTPUT.elf" "${OBJECTS[@]}"

echo "=== Creating BIN ==="
"$OBJCOPY" -O binary "$OUTPUT.elf" "$OUTPUT.bin"

echo "=== Size report ==="
"$SIZE" --format=berkeley "$OUTPUT.elf"

echo ""
echo "Bootloader build complete: $OUTPUT.bin"