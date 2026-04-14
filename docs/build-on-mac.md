# Build on Mac

This guide explains how to build the `openterface-km-firmware` binary on a macOS host (Apple Silicon or Intel).

## Prerequisites

- macOS with Xcode Command Line Tools installed:
  ```bash
  xcode-select --install
  ```
- Homebrew packages required for the bundled command-line flasher:
  ```bash
  brew install libusb pkg-config
  ```

## 1. Download the RISC-V Toolchain

The makefiles reference the `riscv-none-embed-gcc` toolchain name (from WCH's MounRiver Studio). The open-source xpack `riscv-none-elf-gcc` works as a drop-in replacement with a small wrapper.

Download the arm64 (Apple Silicon) build:

```bash
mkdir -p ~/tools/xpack-riscv
curl -L --progress-bar \
  -o ~/tools/xpack-riscv/toolchain.tar.gz \
  "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v15.2.0-1/xpack-riscv-none-elf-gcc-15.2.0-1-darwin-arm64.tar.gz"
```

> For Intel Macs, replace `darwin-arm64` with `darwin-x64`.

Extract:

```bash
cd ~/tools/xpack-riscv
tar -xzf toolchain.tar.gz
```

## 2. Create Toolchain Wrappers

The makefiles call `riscv-none-embed-*` and use `-march=rv32imacxw` (a WCH-proprietary extension). The wrappers remap the tool names and translate the arch flag to the standard `rv32imac_zicsr_zifencei` that GCC 15 understands.

```bash
TOOLBIN=~/tools/xpack-riscv/xpack-riscv-none-elf-gcc-15.2.0-1/bin
WRAPDIR=~/tools/xpack-riscv/wrappers
mkdir -p "$WRAPDIR"

# Generic pass-through wrappers
for tool in g++ objcopy objdump size ar as nm ranlib addr2line readelf strings strip c++filt; do
  printf '#!/bin/sh\nexec "%s/riscv-none-elf-%s" "$@"\n' "$TOOLBIN" "$tool" \
    > "$WRAPDIR/riscv-none-embed-$tool"
  chmod +x "$WRAPDIR/riscv-none-embed-$tool"
done

# GCC wrapper — also strips the proprietary 'xw' arch extension
cat > "$WRAPDIR/riscv-none-embed-gcc" << 'EOF'
#!/bin/bash
TOOLBIN="$HOME/tools/xpack-riscv/xpack-riscv-none-elf-gcc-15.2.0-1/bin"
args=()
for arg in "$@"; do
  args+=("${arg//rv32imacxw/rv32imac_zicsr_zifencei}")
done
exec "$TOOLBIN/riscv-none-elf-gcc" "${args[@]}"
EOF
chmod +x "$WRAPDIR/riscv-none-embed-gcc"
```

## 3. Build

Add the wrappers to your `PATH` and run the helper from the repository root:

```bash
export PATH="$HOME/tools/xpack-riscv/wrappers:$PATH"
cd /path/to/Openterface_KM
./tools/firmware.sh build
```

Direct `make` usage still works if you prefer it:

```bash
export PATH="$HOME/tools/xpack-riscv/wrappers:$PATH"
cd /path/to/Openterface_KM/build
make all
```

### Probe and flash

Put the board into bootloader mode before flashing.

```bash
# Check that the WCH ISP bootloader is visible over USB
./tools/firmware.sh probe

# Build and flash the default firmware
./tools/firmware.sh flash

# Build and flash KeyMod BLE firmware
./tools/firmware.sh flash --clean --board-version 3 --enable-uac 0 --enable-eth 0 --enable-ble 1 --ble-name KeyMod-BLE

# Build and flash a BLE-only image with a custom advertising name
./tools/firmware.sh flash --clean --enable-uac 0 --enable-eth 0 --enable-ble 1 --ble-name Keymod-BLE
```

If you switch `ENABLE_*` flags or `BLE_NAME`, prefer a clean build for that first flash.
If BLE still does not advertise after boot, retry with a larger heap, for example `--ble-memheap-size 8192`.
If you are targeting KeyMod hardware, also pass `--board-version 3` for the BLE build.

### Expected output

```
Memory region         Used Size  Region Size  %age Used
           FLASH:      195648 B       448 KB     42.65%
             RAM:         64 KB        64 KB    100.00%
Finished building target: openterface-km-firmware.elf
```

Output files are written to the `build/` directory:

| File | Description |
|------|-------------|
| `openterface-km-firmware.elf` | ELF with debug symbols, used for flashing and debugging |
| `openterface-km-firmware.bin` | Raw binary image used by the WCH ISP flasher |
| `openterface-km-firmware.hex` | Intel HEX image |
| `openterface-km-firmware.lst` | Disassembly listing |
| `openterface-km-firmware.map` | Linker memory map |

## 4. Clean

```bash
./tools/firmware.sh clean
```

Direct `make` cleanup remains available:

```bash
cd build && make clean
```

## Persistent PATH setup (optional)

To avoid exporting `PATH` every session, add the following to your `~/.zshrc`:

```bash
export PATH="$HOME/tools/xpack-riscv/wrappers:$PATH"
```

## Notes

- The `libwchble.a` BLE library is pre-built and included in the `LIB/` directory. It is a binary for the CH32V20x target and does not need to be compiled from source.
- The `.lst` disassembly file will show a harmless warning (`unrecognized disassembler option: xw`) because `objdump` doesn't know the proprietary extension. This does not affect the firmware.
- RAM usage is 100% by design — the BLE stack is mapped into the upper portion of RAM by the linker script.
- `tools/firmware.sh` auto-builds the bundled `tools/wchisp_c` host flasher when you run `probe` or `flash`.
