# CarrierHasArrived — split board

This branch is the contract for the **split** hoverboard on the CarrierHasArrived base. It is not a firmware image.

A split hoverboard is two half-boards, one hub each. The chip is usually a GD32F130 (or a GD32F103C8 / MM32 part, depending on the board). `hoverboard-firmware-hack-FOC` targets the single STM32F103RCT6 / GD32F103RCT6 mainboard: pin map, linker script, and the dual-motor FOC loop. Flashing this tree onto a split half will not run. `Inc/config.h` is unchanged from main on purpose, so a USART build of this branch is still the single-mainboard image.

The phone, the ESP32, and the serial frame are specified in CarrierHasArrived `design/PLAN.md`. The single mainboard image is branch `cursor/carrier-single-board-bd10`.

## What the ESP32 expects

One UART to the **master** half. 115200 8N1, little-endian, start word `0xABCD`. Same 8-byte command and 18-byte feedback as `VARIANT_USART`.

| Command field | Wheel |
|---------------|--------|
| `steer` | left hub |
| `speed` | right hub |

The master drives its own hub and forwards the other wheel's command to the slave on the link the two halves already share. Feedback must carry **both** measured speeds (`speedL_meas`, `speedR_meas`), pack voltage in volts × 100, and board temperature in °C × 10. The ESP32 does not open a second UART onto the slave.

If frames stop for about 0.8 s, both wheels release (open mode, command 0), including the slave. A master that only stops itself is not done.

10S, 36 V nominal. Start the current limit in the same class as the single board (`I_MOT_MAX` 15 A) and change it only after measuring the motors. Speed mode, so a 0 command holds 0 rpm.

## Where the code has to land

Read the chip marking, then port the frame onto the tree that already boots that part:

| Chip | Firmware the EFeru wiki points at |
|------|-----------------------------------|
| STM32F130 / GD32F130C8 / GD32F130C6 | Hoverboard-Firmware-Hack-Gen2 |
| GD32F103C8T6 | the GD32F103 split port linked from the wiki |
| MM32SPIN05 / 06 / 25 | the MM32 split port linked from the wiki |
| GD32F130K6 | whichever repo currently builds for that part |

The port keeps that project's pin map and motor control. It adds the USART command parser, the slave forward, and the combined feedback. It does not move the Gen2 sources into this repository.

Until that port exists, the ESP32 treats a silent split UART as a dead link and commands the single mainboard to 0 as well.

## Bring-up once a port exists

1. Master alone, slave disconnected: the ESP32 link stays down (feedback must include both wheels, or the master must report the missing wheel as a fault and zero itself).
2. Both halves, wheels off the ground: positive `steer` turns the left hub platform-forward, positive `speed` turns the right. Sign errors are corrected on the ESP32.
3. Pull the ESP32 UART. Both hubs coast within a second.
