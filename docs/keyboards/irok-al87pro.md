# IROK AL87 Pro

## Specs

- Keyboard: IROK AL87 Pro (87-key TKL layout)
- MCU: BYK916 ([SH68F90A](../platforms/sh68f90.md))
- Wireless: BK3632 (2.4GHz proprietary RF + Bluetooth BDR/BLE) via SPI co-processor link
- Matrix: 6 rows × 17 columns (87 keys, full NKRO support)
- Backlight: Per-key RGB LED Matrix (6 rows × 17 columns) via 18 hardware PWM channels
- Indicators: Caps Lock, Win Lock, connection and battery status LEDs
- Power: Rechargeable Li-Ion battery with USB VBUS sense (P4.4) and charge status (P7.7)

## SMK Supported Features

- [x] Key matrix scanning (6×17 TKL 87-key)
- [x] USB HID full-speed keyboard and media controls
- [x] Hardware PWM per-key RGB backlight and indicators
- [x] Wireless co-processor link (BK3632 SPI)
- [x] Inactivity sleep and low-power management
- [x] ISP bootloader chord (`S` + `M` + `K` held at plug-in)

## Matrix

| | Pins |
|---|---|
| Rows (inputs, pull-up) | R0–R3 = P7.0–P7.3, R4 = P5.3, R5 = P5.4 |
| Columns (active low) | C0–C5 = P6.2–P6.7, C6–C9 = P5.0–P5.2, P5.7, C10–C16 = P4.0–P4.3, P4.5–P4.7 |

## Code Options

This firmware requires the following code options programmed on the BYK916 (SH68F90A):

```
Code Option String: A4E063C00F000088
Code Option Number: 0x8800000fc063e0a4
```

## Flashing

### Application firmware via ISP (`sinowisp`)

```bash
sinowisp write --platform sh68f90 --vendor_id 0x1209 --product_id 0x9160 --isp_iface_num 2 --isp_report_id 5 --reboot true build/irok-al87pro_default_smk.hex
```

### Full firmware via ICP (`sinodude`)

```bash
sinodude -p sh68f90a -c 0x8800000fc063e0a4 -f build/irok-al87pro_default_full.hex
```
