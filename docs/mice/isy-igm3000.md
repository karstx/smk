# ISY IGM3000

A wired gaming mouse with a single RGB zone and four DPI indicator LEDs. This port is a
standalone application on `sino51lib` (it does not use the keyboard core), written
through the stock ISP bootloader with [sinowisp](https://github.com/karstx/sinowisp).

## Specs

- MCU: [SH68F881](../platforms/sh68f881.md), stock ISP bootloader MD5 `13df4ce2933f9654ffef80d6a3c27199`
- USB: `258a:1007` (stock firmware and this one), `0603:1020` in ISP mode
- Buttons: Left, Right, Middle, G4, G5, G7, DPI+, DPI-, plus a scroll wheel
- Sensor: one of four variants, detected at boot (see [Sensors](#sensors))
- Lighting: one RGB zone on hardware PWM, four single-colour DPI LEDs

## Supported Features

- [x] 1000 Hz reports, 16-bit motion, sensor read continuously between USB polls
- [x] All four sensor variants the stock firmware supports, auto-detected
- [x] Six DPI levels with the stock DPI LED patterns
- [x] Scroll wheel
- [x] RGB effects: rainbow (default), breathing, static, off
- [x] ISP report compatible with stock, so unmodified sinowisp reflashes it
- [ ] Button remapping
- [ ] Settings persistence (DPI level and LED mode reset on replug)

Default buttons: G4 is Back, G5 is Forward, G7 cycles the RGB effect, DPI+/DPI- step
through the levels. The table is `BUTTON_MAP_INIT` in `src/mice/isy-igm3000/board.h`.

## Building and Flashing

```sh
meson compile -C build isy-igm3000_smk.hex
meson compile -C build isy-igm3000_flash    # sinowisp write via the running firmware
```

The flash target is equivalent to:

```sh
sinowisp write -p sh68f881 --vendor_id 0x258a --product_id 0x1007 --force isy-igm3000_smk.hex
```

**Back up the stock firmware first**; sinowisp reads the application region through the
same bootloader:

```sh
sinowisp read -p sh68f881 --vendor_id 0x258a --product_id 0x1007 --format bin stock.bin
```

Writing `stock.bin` back with the same `write` command restores the original firmware.

## Getting Back Into ISP Mode

There is no ICP header on this mouse, so every recovery path goes through the stock
bootloader:

- sinowisp's ISP command (feature report 5, `05 75`) on interface 1, as with stock;
- holding two or more buttons scanned in the same phase (Left + Right works) while
  plugging in jumps to the bootloader before USB is started;
- if the host never configures the device within 10 s of power-on, the firmware jumps to
  the bootloader on its own. This only arms until the first successful configuration.

The firmware never writes flash or EEPROM.

## Hardware

Recovered from the stock firmware; addresses refer to the stock application image.

| Function | Pins | Notes |
| --- | --- | --- |
| Buttons, direct | P2.1, P2.2, P2.3, P6.6, P6.7 | bits 0-4: Left, Right, Middle, G4, G5 |
| Buttons, matrix | P2.1, P2.2, P2.3 with P3.0 driven low | bits 5-7: DPI+, DPI-, G7; only scanned when bits 0-4 are idle |
| Wheel | P0.7, P0.6 | quadrature, detents at `00` and `11` |
| Sensor bus | P2.4 NCS, P2.6 SCK, P2.5/P2.7 data | bit-banged, every line driven open-drain style on the pull-ups |
| RGB | PWM0 red, PWM1 green, PWM2 blue | 12-bit period `0x0FFF`, `PWMxCON = 0xC1` |
| DPI LEDs | P4.0-P4.3, active low | stock patterns `01 03 07 0C 0E 0F` for levels 1-6 (`0x4109`) |
| unknown | P7.0 | output low, as stock sets it |

All buttons are active low. P0.1 and P0.5 are inputs without pull-ups and are only touched
by the stock suspend/resume path.

### Sensors

The stock firmware probes four sensor and PCB variants at every boot (`0x214F`) and this
port does the same, in the same order. Each probe writes `0x3A = 0x5A` and reads the
product ID from register `0x00`:

| Type | Product ID | Bus | Motion | CPI register |
| --- | --- | --- | --- | --- |
| 2 | `0x09` | 4-wire: MOSI P2.5, MISO P2.7 | `0x02` bit 7, `0x03`/`0x04` 8-bit | `0x33` |
| 1 | `0x12` | 3-wire SDIO on P2.7, P2.5 pulsed low then held high | as type 2 | `0x19` |
| 4 | `0x3B` | 3-wire SDIO on P2.7 | 12-bit: `0x03`, `0x04`, high nibbles in `0x0C` | `0x0D` |
| 11 | `0x43` | 3-wire SDIO on P2.7 | 16-bit burst from `0x16` | `0x1B` |

The tested unit has type 11. The init tables, DPI tables and the software doubling above
each sensor's native range are copied from stock (see `sensor.c`). Y is negated for every
type except 11, matching the stock default orientation.

### Stock Behaviour Worth Knowing

- Buttons are scanned every 8 ms (USB SOF divided by 8) and accepted after three equal
  samples: roughly 16-24 ms from press to report. This port scans every 1 ms and accepts
  after four equal samples (3-4 ms).
- Key assignments and DPI levels are loaded into XRAM at boot from configuration that is
  not in the code flash (most likely the separate EEPROM block), so an ISP read does not
  contain the stock defaults.

## Platform Findings

Two things that apply to SH68F881 work beyond this mouse:

- **Timer2 counts at FREQ_SYS/6 (2 MHz) here.** The `0xFF7D` reload in
  `sino51lib/sh68f881/systick.c` (written for a measured ~262 kHz) produced a 0.5 ms tick
  that ran 7.6x fast on this mouse; `0xFC18` measured 1.0004 ms against the host clock.
- **Stock firmware enters the bootloader from inside its USB ISR,** and the bootloader
  starts the new application with a plain jump, never a RETI. The first boot after
  flashing from stock therefore starts with an interrupt still marked in service and no
  interrupt can fire. `main.c` clears it with a few dummy RETIs before enabling anything.
  Keyboard targets flashed from a stock firmware that does the same may be affected too.

## Tools

[`tools/igm3000`](../../tools/igm3000/README.md) holds the pre-flash emulator/verifier,
the disassembler used for this port and a hidraw diag tool for the running firmware.
