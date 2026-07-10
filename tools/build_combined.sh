#!/usr/bin/env bash
# Build combined firmware: bootloader (16KB padded) + application
# Usage: ./tools/build_combined.sh [BOARD_VERSION]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BOARD_VERSION="${1:-1}"

echo "=== Building bootloader ==="
bash "$REPO_ROOT/Bootloader/build.sh"

echo ""
echo "=== Building application ==="
BOARD_VERSION="$BOARD_VERSION" bash "$REPO_ROOT/tools/build_firmware.sh"

echo ""
echo "=== Creating combined firmware ==="

BL_BIN="$REPO_ROOT/Bootloader/build/bootloader.bin"
APP_BIN="$REPO_ROOT/obj/openterface-km-firmware.bin"
COMBINED="$REPO_ROOT/obj/firmware_combined.bin"

# Pad bootloader to 16KB (16384 bytes)
BL_PADDED="$REPO_ROOT/Bootloader/build/bootloader_padded.bin"
dd if=/dev/zero bs=16384 count=1 2>/dev/null > "$BL_PADDED"
dd if="$BL_BIN" of="$BL_PADDED" conv=notrunc 2>/dev/null

# Concatenate
cat "$BL_PADDED" "$APP_BIN" > "$COMBINED"

echo "Bootloader: $(wc -c < "$BL_BIN") bytes → padded to 16384 bytes"
echo "Application: $(wc -c < "$APP_BIN") bytes"
echo "Combined: $(wc -c < "$COMBINED") bytes → $COMBINED"
echo ""
echo "Flash with:"
echo "  wchisp flash $COMBINED"