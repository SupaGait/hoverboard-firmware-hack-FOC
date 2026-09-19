# AGENTS.md

## Cursor Cloud specific instructions

This repository is **STM32 embedded firmware** (hoverboard-firmware-hack-FOC) for the
STM32F103RCT6 / GD32F103RCT6 mainboard. There is no runnable server or app and no automated
test suite — "running the application" means **cross-compiling the firmware** into flashable
`.elf`/`.hex`/`.bin` artifacts. Flashing (`make flash`, `pio run -t upload`) requires physical
ST-Link hardware and is not possible in the cloud VM.

### Toolchains (installed during environment setup, persisted in the snapshot)
- `arm-none-eabi-gcc` (Ubuntu package, GCC 13.2) — used by the `make` build.
- `platformio` (`pio`), installed via `pip install --user`; lives in `~/.local/bin`, which is
  already on the login-shell `PATH`. On first `pio run` it downloads the `ststm32` platform and
  its own ARM toolchain into `~/.platformio` (cached in the snapshot).

### Build commands
- Make build (single variant, mirrors CI): `make -e VARIANT=VARIANT_ADC`
  - Outputs to `build/hover.{elf,hex,bin}`. Run `make clean` before switching variants, since the
    variant is baked in via a `-D` flag and `make` will not otherwise rebuild.
- PlatformIO build (all variants): `pio run` — builds all 10 `VARIANT_*` envs into
  `.pio/build/<VARIANT>/firmware.{elf,bin}`.
- PlatformIO single variant: `pio run -e VARIANT_ADC`.

### Expected non-fatal warnings
- The `make` link step prints newlib warnings like `_close is not implemented and will always
  fail` / `_read is not implemented ...`. These are **normal** for bare-metal newlib-nano and do
  not indicate a build failure (exit code is 0 and binaries are produced).

### Lint / format
- There is no CI lint step. The only formatting helper is `make format`, which runs
  `clang-format -i` in place over `Src/` and `Inc/` (requires `clang-format`; not run in CI).

### CI reference
- `.github/workflows/build_on_commit.yml` runs `make` (with `VARIANT=VARIANT_ADC`) and `pio run`.
