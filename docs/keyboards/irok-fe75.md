# IROK FE75 Pro

## Specs

- MCU: [SH68F90A](../platforms/sh68f90.md) (reported by the debug console as `PART 68f90a`; package label not checked)
- ISP bootloader: MD5 `3e0ebd0c440af5236d7ff8872343f85d`
- USB ID (stock): `258a:00fc` ("BY Tech" / "Gaming Keyboard"), kept by this port
- Layout: 75% ANSI, 81 keys
- Backlight: per-key RGB
- Switches: 3-position slider (Bluetooth / USB / 2.4G)
- Wireless: BK3632-compatible (BT and 2.4G), same protocol as the NuPhy Air60

## SMK Supported Features

- [x] Key Scan
- [x] RGB Matrix
- [x] Wireless (BT and 2.4G, tested with a phone and the 2.4G dongle)
- [x] Sleep (wireless on battery only)

## Matrix

6 rows × 16 columns. The stock firmware scans 21 columns because it's shared with a larger board.
C16–C20 have no switches on the FE75 Pro and aren't scanned.

| | Pins |
|---|---|
| Rows (inputs, pull-up) | R0–R3 = P7.0–P7.3, R4 = P5.3, R5 = P5.4 |
| Columns (active low) | C0–C7 = P6.0–P6.7, C8–C11 = P5.0/P5.1/P5.2/P5.7, C12–C15 = P4.0/P4.2/P4.3/P4.5 |

## Scanning

- **Debounce:** `MATRIX_DEBOUNCE_SCANS 3`, eager and per key. A change is reported on the first sweep that sees it, then that key ignores its contacts for 3 sweeps (~6 ms). Without debounce the switches chatter into double presses. A per-column deferred version reordered and dropped keys at full typing speed.
- **Event order:** `MATRIX_EVENT_QUEUE 16`. Key changes are replayed in the order the scan saw them. Diffing a snapshot processed changes that landed between main-loop passes in column order, so a fast Shift + letter came out lowercase.
- **LED refresh:** `LED_SUBFRAMES_PER_SCAN 4` gives about 125 Hz, a key sweep every ~2 ms, and LED effects recomputed 8 LEDs per main-loop pass.

## RGB

The key columns are also the LED commons. 18 PWM channels carry R, G and B for the six rows:
P1.0–P1.5 (PWM20–25), P2.0–P2.5 (PWM10–15), P3.3–P3.5 (PWM03–05) and P3.0–P3.2 (PWM00–02), in
that channel order. Row 4's LEDs aren't in key order: Z's LED is on common C12.

## Sleep

- **When:** only in the 2.4G and BT positions, only when no host has the USB link up, after `SLEEP_TIMEOUT` (21000 LED frames, about 3 minutes) without a key change. USB mode never sleeps.
- **Why the cable rule:** the slider pins aren't INT4 wake sources, so a sleeping board can't notice the slider moving back to USB. With a cable the board is powered from USB anyway, so it stays awake and the slider stays live.
- **Wake:** any key. INT4 bits measured on the board: P7.0 = 0, P7.1 = 1, P7.2 = 4, P7.3 = 5, P5.3 = 6, P5.4 = 7, which are exactly the channels `IENC = 0xF3` enables.
- **Parking:** follows the stock sleep entry at `0x9571`: columns low, P0.0/P0.1, P5.5/P5.6 and P7.7 low with pull-ups off, wireless pins released. P4.1 (RF ACK) is left alone.

## Mode Slider

| Position | P5.5 | P5.6 |
|---|---|---|
| USB | high | high |
| 2.4G | low | high |
| Bluetooth | high | low |

## Wireless

| Signal | Pin |
|---|---|
| CS | P7.5 |
| SCK | P4.7 |
| MISO | P0.3 |
| MOSI | P0.4 |
| MOT | P0.2 |
| ACK | P4.1 |

## Default Fn Layer

| Fn + | Action |
|---|---|
| F1 / F2 / F3 | Mute / volume down / volume up |
| F4 | Backlight on/off |
| F5 / F6 | Backlight brighter / dimmer |
| F7 / F8 | Effect slower / faster |
| F9 | Windows-key lock |
| F10 / F11 | Web home / My Computer |
| F12 | Next effect |
| 1 / 2 / 3 | Bluetooth host 1–3 (hold ~3 s to pair) |
| 4 | 2.4G link (hold ~3 s to pair) |
| Ins / Del / PgUp / PgDn | Scroll Lock / Pause / Home / End |

## Code Options

```
Code Option String: A4E063C00F000088
```

The same as the NuPhy Air60.
