#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
DEFAULT_WRAPPER_DIR="$HOME/tools/xpack-riscv/wrappers"
MAKE_BIN="${MAKE:-make}"

usage() {
    cat <<'EOF'
Usage:
  ./tools/firmware.sh build [options] [MAKE_VAR=value ...]
  ./tools/firmware.sh flash [options] [MAKE_VAR=value ...]
  ./tools/firmware.sh probe
  ./tools/firmware.sh clean

Options:
  --clean                 Run 'make clean' before build or flash.
    --board-version <1-4>   Set board firmware variant: 1=kvm go, 2=mini kvm, 3=keymod BLE, 4=keymod ECM.
  --enable-ble <0|1>      Set ENABLE_BLE.
  --enable-uac <0|1>      Set ENABLE_UAC.
  --enable-eth <0|1>      Set ENABLE_ETH.
  --ble-name <name>       Set BLE_NAME.
  --ble-memheap-size <n>  Set BLE_MEMHEAP_SIZE.
  --ble-buff-max-len <n>  Set BLE_BUFF_MAX_LEN.
  --ble-tx-num-event <n>  Set BLE_TX_NUM_EVENT.
  -h, --help              Show this help text.

Examples:
  ./tools/firmware.sh build
  ./tools/firmware.sh probe
  ./tools/firmware.sh flash
    ./tools/firmware.sh flash --clean --board-version 3 --enable-uac 0 --enable-eth 0 --enable-ble 1 --ble-name KeyMod-BLE
  ./tools/firmware.sh flash --enable-uac 0 --enable-eth 0 --enable-ble 1 --ble-name Keymod-BLE
  ./tools/firmware.sh build ENABLE_ETH=1 ENABLE_BLE=0
EOF
}

ensure_make() {
    if ! command -v "$MAKE_BIN" >/dev/null 2>&1; then
        echo "make not found: $MAKE_BIN" >&2
        exit 1
    fi
}

ensure_toolchain() {
    if ! command -v riscv-none-embed-gcc >/dev/null 2>&1 && [[ -d "$DEFAULT_WRAPPER_DIR" ]]; then
        export PATH="$DEFAULT_WRAPPER_DIR:$PATH"
    fi

    if ! command -v riscv-none-embed-gcc >/dev/null 2>&1; then
        echo "riscv-none-embed-gcc not found in PATH." >&2
        echo "Install the xPack wrapper toolchain first; see docs/build-on-mac.md." >&2
        exit 1
    fi
}

run_make() {
    (
        cd "$BUILD_DIR"
        "$MAKE_BIN" "$@"
    )
}

if [[ $# -eq 0 ]]; then
    usage
    exit 1
fi

command="$1"
shift

clean_first=0
make_vars=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)
            clean_first=1
            ;;
        --board-version)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
            make_vars+=("BOARD_VERSION=$2")
            shift
            ;;
        --enable-ble)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
            make_vars+=("ENABLE_BLE=$2")
            shift
            ;;
        --enable-uac)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
            make_vars+=("ENABLE_UAC=$2")
            shift
            ;;
        --enable-eth)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
            make_vars+=("ENABLE_ETH=$2")
            shift
            ;;
        --ble-name)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
            make_vars+=("BLE_NAME=$2")
            shift
            ;;
        --ble-memheap-size)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
            make_vars+=("BLE_MEMHEAP_SIZE=$2")
            shift
            ;;
        --ble-buff-max-len)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
            make_vars+=("BLE_BUFF_MAX_LEN=$2")
            shift
            ;;
        --ble-tx-num-event)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
            make_vars+=("BLE_TX_NUM_EVENT=$2")
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *=*)
            make_vars+=("$1")
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
    shift
done

ensure_make

case "$command" in
    build)
        ensure_toolchain
        if [[ $clean_first -eq 1 ]]; then
            run_make clean
        fi
        run_make "${make_vars[@]}" all
        ;;
    flash)
        ensure_toolchain
        if [[ $clean_first -eq 1 ]]; then
            run_make clean
        fi
        run_make "${make_vars[@]}" flash
        ;;
    probe)
        run_make probe
        ;;
    clean)
        run_make clean
        ;;
    help)
        usage
        ;;
    *)
        echo "Unknown command: $command" >&2
        usage
        exit 1
        ;;
esac