#!/usr/bin/env bash
# =============================================================================
# Openterface KM Firmware Flash Script
# Flashes firmware to CH32V20x device via USB ISP bootloader.
#
# Usage:
#   ./tools/flash.sh [options]
#   ./tools/flash.sh kvmgo              # Flash KVMGo firmware
#   ./tools/flash.sh --tool wchisp      # Force use wchisp
#   ./tools/flash.sh --no-reset         # Don't reset device after flash
#
# Requirements:
#   - Device in BOOT/ISP mode (PID 0x55E0)
#   - wchisp or WCHISPTool_CMD installed
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# --- Defaults ---
WCHISP_BIN="${WCHISP_BIN:-$HOME/.local/bin/wchisp}"
WCHISPTOOL_CMD="${WCHISPTOOL_CMD:-$HOME/project/WCHISPTool_CMD/macOS/bin/WCHISPTool_CMD}"
WCHISP_CONFIG="${WCHISP_CONFIG:-$ROOT_DIR/tools/flash_clean.ini}"
FIRMWARE_FILE=""
TARGET=""
TOOL="auto"        # auto | wchisp | wchisp_cmd
DO_RESET=1
DRY_RUN=0

# --- Colors ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

usage() {
    cat <<EOF
${BLUE}Openterface KM Flash Tool${NC}

Usage: $(basename "$0") [options] [target]

Targets:
  kvmgo          Flash KVMGo firmware (BOARD_VERSION=1)
  minikvm        Flash Mini-KVM firmware (BOARD_VERSION=2)
  keymod-plus    Flash KeyMod Plus firmware
  keymod-mini    Flash KeyMod Mini firmware

Options:
  --tool <wchisp|wchisp_cmd>  Force specific flash tool (default: auto)
  --firmware <path>           Custom firmware file (.bin or .hex)
  --no-reset                  Don't reset device after flashing
  --dry-run                   Print commands without executing
  -h, --help                  Show this help

Examples:
  $(basename "$0") kvmgo                          # Flash KVMGo firmware
  $(basename "$0") --tool wchisp kvmgo            # Force use wchisp
  $(basename "$0") --firmware custom.bin          # Flash custom firmware file

Device must be in BOOT/ISP mode:
  - USB PID 0x55E0, Vendor 0x4348
  - Hold BOOT button while plugging in USB
EOF
}

log()  { printf "${BLUE}[flash]${NC} %s\n" "$*"; }
ok()   { printf "${GREEN}[flash]${NC} %s\n" "$*"; }
warn() { printf "${YELLOW}[flash]${NC} %s\n" "$*"; }
err()  { printf "${RED}[flash]${NC} %s\n" "$*" >&2; }

run() {
    if [[ "$DRY_RUN" -eq 1 ]]; then
        printf '+ %q\n' "$@"
        return 0
    fi
    "$@"
}

# --- Argument parsing ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        kvmgo|minikvm|keymod-plus|keymod-mini)
            TARGET="$1"; shift ;;
        --tool)       TOOL="$2"; shift 2 ;;
        --firmware)   FIRMWARE_FILE="$2"; shift 2 ;;
        --no-reset)   DO_RESET=0; shift ;;
        --dry-run)    DRY_RUN=1; shift ;;
        -h|--help)    usage; exit 0 ;;
        *)            err "Unknown argument: $1"; usage; exit 1 ;;
    esac
done

# --- Resolve firmware file ---
resolve_firmware() {
    if [[ -n "$FIRMWARE_FILE" ]]; then
        if [[ ! -f "$FIRMWARE_FILE" ]]; then
            err "Firmware file not found: $FIRMWARE_FILE"
            exit 1
        fi
        return
    fi

    if [[ -z "$TARGET" ]]; then
        err "No target specified. Use: $(basename "$0") kvmgo|minikvm|keymod-plus|keymod-mini"
        err "   Or: $(basename "$0") --firmware <path>"
        exit 1
    fi

    local targets=("output" "build")
    local bin="" hex=""

    for dir in "${targets[@]}"; do
        bin="$ROOT_DIR/$dir/openterface-km-firmware-${TARGET}.bin"
        hex="$ROOT_DIR/$dir/openterface-km-firmware-${TARGET}.hex"
        [[ -f "$bin" || -f "$hex" ]] && break
    done

    if [[ ! -f "$bin" && ! -f "$hex" ]]; then
        err "Firmware not found for target: $TARGET"
        err "Build first: ./build.sh $TARGET"
        err "Searched in: output/ and build/"
        exit 1
    fi

    # Prefer .bin for wchisp, .hex for WCHISPTool_CMD
    if [[ -f "$bin" ]]; then
        FIRMWARE_FILE="$bin"
    else
        FIRMWARE_FILE="$hex"
    fi
}

# --- Detect available tool ---
detect_tool() {
    local wchisp_ok=0
    local cmd_ok=0

    [[ -x "$WCHISP_BIN" ]] && command -v "$WCHISP_BIN" &>/dev/null && wchisp_ok=1
    [[ -x "$WCHISPTOOL_CMD" ]] && command -v "$WCHISPTOOL_CMD" &>/dev/null && cmd_ok=1

    case "$TOOL" in
        wchisp)
            [[ $wchisp_ok -eq 0 ]] && { err "wchisp not found at $WCHISP_BIN"; exit 1; }
            echo "wchisp"
            ;;
        wchisp_cmd)
            [[ $cmd_ok -eq 0 ]] && { err "WCHISPTool_CMD not found at $WCHISPTOOL_CMD"; exit 1; }
            echo "wchisp_cmd"
            ;;
        auto)
            if [[ $wchisp_ok -eq 1 ]]; then
                echo "wchisp"
            elif [[ $cmd_ok -eq 1 ]]; then
                echo "wchisp_cmd"
            else
                err "No flash tool found."
                err "Install wchisp:  brew install wchisp  OR  cargo install wchisp"
                err "Or download WCHISPTool_CMD from WCH official site."
                exit 1
            fi
            ;;
    esac
}

# --- Wait for device ---
wait_for_device() {
    local max_wait=15
    local waited=0

    log "Waiting for device in BOOT mode (max ${max_wait}s)..."

    while [[ $waited -lt $max_wait ]]; do
        # Check with system_profiler
        if system_profiler SPUSBDataType 2>/dev/null | grep -q "55[eE]0"; then
            ok "Device detected!"
            return 0
        fi

        # Check with wchisp probe (if using wchisp)
        if [[ "$FLASH_TOOL" == "wchisp" ]] && "$WCHISP_BIN" probe 2>/dev/null | grep -qi "found\|chip"; then
            ok "Device detected via wchisp!"
            return 0
        fi

        sleep 1
        waited=$((waited + 1))
        printf "."
    done

    printf "\n"
    err "Device not detected after ${max_wait}s."
    err ""
    err "Troubleshooting:"
    err "  1. Ensure device is plugged in via USB"
    err "  2. Enter BOOT mode: hold BOOT button while plugging in, or"
    err "     pull BOOT pin high during reset"
    err "  3. Check System Report: USB PID should be 0x55E0, Vendor 0x4348"
    err "  4. Try a different USB port or cable"
    err ""
    err "Check macOS System Report:"
    err "  system_profiler SPUSBDataType | grep -A 10 -i 'wch\|4348\|55e0'"
    return 1
}

# --- Flash with wchisp ---
flash_wchisp() {
    local fw="$1"
    log "Flashing with wchisp: $fw"

    run "$WCHISP_BIN" flash "$fw"

    if [[ $? -eq 0 ]]; then
        ok "Flash successful!"
    else
        err "Flash failed with wchisp"
        return 1
    fi
}

# --- Flash with WCHISPTool_CMD ---
flash_wchisp_cmd() {
    local fw="$1"
    log "Flashing with WCHISPTool_CMD: $fw"

    # Remove code protection first
    log "Removing code protection..."
    run "$WCHISPTOOL_CMD" \
        -c "$WCHISP_CONFIG" \
        -o program \
        -r \
        -f "$fw" 2>&1

    local rc=$?

    if [[ $rc -eq 0 ]]; then
        ok "Flash successful!"
    elif [[ $rc -eq 6 ]]; then
        err "Device not found (Code 6). Possible causes:"
        err "  - Device not in BOOT mode"
        err "  - Chip type mismatch in config: check $WCHISP_CONFIG"
        err "    Current: CH32V208GBU - adjust if your chip is different"
        return 1
    elif [[ $rc -eq 12 ]]; then
        err "Download failed (Code 12). Try removing code protection first:"
        err "  $WCHISPTOOL_CMD -c $WCHISP_CONFIG -r"
        return 1
    else
        err "Flash failed with code $rc"
        return 1
    fi
}

# --- Main ---
main() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  Openterface KM Flash Tool${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""

    resolve_firmware
    FLASH_TOOL=$(detect_tool)

    log "Target:      ${TARGET:-custom}"
    log "Firmware:    $FIRMWARE_FILE"
    log "Flash tool:  $FLASH_TOOL"
    log "Reset after: $DO_RESET"
    echo ""

    # Check device
    if ! wait_for_device; then
        exit 1
    fi

    # Flash
    case "$FLASH_TOOL" in
        wchisp)     flash_wchisp "$FIRMWARE_FILE" ;;
        wchisp_cmd) flash_wchisp_cmd "$FIRMWARE_FILE" ;;
    esac

    local rc=$?

    if [[ $rc -eq 0 ]]; then
        echo ""
        ok "============================================"
        ok "  Flash complete!"
        ok "============================================"
        echo ""
        log "Device should now enumerate as a USB HID device."
        log "Check: system_profiler SPUSBDataType | grep -i openterface"
    fi

    exit $rc
}

main
