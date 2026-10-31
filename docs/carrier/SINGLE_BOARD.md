# CarrierHasArrived — single mainboard

Flash this branch on the **single** hoverboard mainboard (STM32F103RCT6 or GD32F103RCT6) that drives both hubs. The split hoverboard is a different branch and a different MCU; do not flash this image there.

The phone and the ESP32 are specified in CarrierHasArrived `design/PLAN.md`. This file is only what changed in the firmware.

## What the ESP32 sends

`VARIANT_USART` on the left sensor cable (USART2), 115200 8N1. The 8-byte command is unchanged. With tank steering:

- `steer` → left wheel (`cmdL`)
- `speed` → right wheel (`cmdR`)

The mixer (`SPEED_COEFFICIENT` / `STEER_COEFFICIENT`) is not used. Sign is fixed on the ESP32 per wheel, so `INVERT_L_DIRECTION` and `INVERT_R_DIRECTION` stay at the stock defaults.

Feedback stays the 18-byte frame (`speedL_meas`, `speedR_meas`, `batVoltage` in volts × 100, `boardTemp` in °C × 10).

## Config relative to main

| Define | On this branch |
|--------|----------------|
| `TANK_STEERING` | on, inside `VARIANT_USART` |
| `CTRL_MOD_REQ` | `SPD_MODE` (forced in `VARIANT_USART`; ±1000 cmd = ±1000 rpm) |
| `RATE` | 32767 (limiter open; the ESP32 ramps) |
| `FILTER` | 65535 (input filter open) |
| `INACTIVITY_TIMEOUT` | 65535 minutes, so an armed base does not latch off after 8 idle minutes |
| `BEEPS_BACKWARD` | 0 |

Left as they already were on main, and required by the plan:

| Define | Value |
|--------|--------|
| `CONTROL_SERIAL_USART2` / `FEEDBACK_SERIAL_USART2` | on |
| `CTRL_TYP_SEL` | `FOC_CTRL` |
| `CTRL_MOD_REQ` | `SPD_MODE` (0 command holds 0 rpm; also set globally on this branch) |
| `BAT_CELLS` | 10 (36 V nominal) |
| `I_MOT_MAX` | 8 A (matches Gen2 board B Carrier `DC_CUR_LIMIT`; `I_DC_MAX` = 10 A) |
| `FIELD_WEAK_ENA` | 0 |
| `SERIAL_TIMEOUT` | 160 (~0.8 s) → open mode if the ESP32 goes quiet |

`INACTIVITY_TIMEOUT` and `BEEPS_BACKWARD` are global, so every variant built from this branch inherits them. Build `VARIANT_USART` for the platform.

## Build

```bash
make -e VARIANT=VARIANT_USART
# or
pio run -e VARIANT_USART
```

App image links at `0x08004000` (after the 16 KiB UART IAP bootloader). Flash the bootloader once, then the app:

```bash
make -C Bootloader flash          # 0x08000000
make flash                        # app at 0x08004000
# or: make flash-all
```

Later app updates go through Carrier SoftAP (`POST /update-board?target=A`). Protocol: CarrierHasArrived `docs/BOARD_IAP.md`.

Wheel direction is checked on a stand with one wheel commanded at a time. The ESP32 stores the invert. Do not chase direction by editing this file per board.
