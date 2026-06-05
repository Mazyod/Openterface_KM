# macOS / Linux Build Notes

## Current Validated Workflow (2026-06-03)

For the BLE-Eth / ECM bridge firmware validation work in this repository, the reliable workflow is:

- build on Windows (MounRiver Studio toolchain)
- flash locally (macOS / Linux) via `wchisp`

See [docs/build-flash-cycle.md](build-flash-cycle.md).

## Local Builds on macOS / Linux

There are existing helper scripts and makefiles (for example `tools/firmware.sh` and `build/makefile`) that can build with a local RISC-V GCC toolchain.

However, recent validation found that local xpack-based builds can produce non-booting binaries for the configurations used in the BLE-Eth / ECM bridge work. If you need a deterministic build for hardware validation, prefer the Windows build flow above.

If you still want to try a local build:

- ensure `riscv-none-embed-gcc` (or the repo’s wrapper name) is on your `PATH`
- prefer clean builds when switching flags: `make clean && make all`

## Troubleshooting

- `riscv-none-embed-gcc` not found: use the Windows build flow in [docs/build-flash-cycle.md](build-flash-cycle.md), or install/activate your local toolchain and wrappers.
