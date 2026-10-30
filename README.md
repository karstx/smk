<div align="center">
  <img src="https://github.com/karstx/smk/assets/498906/30535a69-47a5-4229-8e08-fe2a840d8355" alt="SMK" />
</div>

# SMK - Small (device) Mechanical Keyboard Firmware

[![Build](https://github.com/karstx/smk/actions/workflows/build.yml/badge.svg)](https://github.com/karstx/smk/actions/workflows/build.yml) [![](https://img.shields.io/badge/discord-SMK-blue)](https://discord.gg/SZFBDBuxrK)

This is a keyboard firmware targeting SinoWealth 8051-based devices.

The S (Small) in SMK comes from this firmware using [SDCC](https://sdcc.sourceforge.net/) to build itself.

## ⚠️ WARNING ⚠️

This firmware is still highly experimental, so be cautious when trying to use it or extend it.

You can very easily end up with a bricked device if the written firmware can't jump back into ISP mode, so before testing or modifying it, it's best to have a full dump of your stock firmware and a programming tool (like an Arduino Nano + [sinodude-serial](https://github.com/karstx/sinodude/tree/main/firmware) or SinoWealth SinoLink + ProWriter) that can write it back.

SMK ships [its own ISP bootloader](docs/bootloader.md), which takes that failure mode away: holding **S + M + K** while plugging the board in gets you into ISP mode whatever the firmware above it does. It has to go in over the programming interface once, and from then on the device is recoverable without opening the case.

## Supported Devices

| Keyboard | MCU | Wireless | Extra | Details |
| -------- | --- | -------- | ----- | ------- |
| NuPhy Air60 v1 | SH68F90A / BYK916 | 2.4G, BT (BDR+BLE) | | [Details](docs/keyboards/nuphy-air60.md) |
| E-YOOSO Z11 | SH68F90A / BYK901 | N/A | | [Details](docs/keyboards/eyooso-z11.md) |
| Genesis Thor 300 | SH68F881 / BYK801 | N/A | | [Details](docs/keyboards/genesis-thor-300.md) |
| CIY X77 | SH68F89 / BYK816 | N/A | External 24Cxx EEPROM | [Details](docs/keyboards/ciy-x77.md) |

Also: [ISP bootloader](docs/bootloader.md). Platform notes: [SH68F90 / SH68F90A](docs/platforms/sh68f90.md), [SH68F881](docs/platforms/sh68f881.md), [SH68F89](docs/platforms/sh68f89.md).

## Developing

### Prerequisites

#### Nix

Currently, this project is primarily developed with the help of [Nix](https://nixos.org/) and Nix flakes. Please consider using Nix and the provided [flake](https://github.com/karstx/smk/blob/master/flake.nix) to automatically set up a reproducible development environment.

With Nix installed and flakes enabled, use `nix develop` or [direnv](https://direnv.net/) to enter a shell with all prerequisites installed.

### Building & Flashing

Once all prerequisites are set up, you can build and flash firmware for a specific combination of keyboard and layout using the following commands:

```sh
meson setup build # configure meson build dir
meson compile -C build nuphy-air60_default_smk.hex # build firmware for nuphy-air60 with the default layout
meson compile -C build nuphy-air60_default_flash # write firmware to the device via sinowisp
```

The same build produces `nuphy-air60_boot.hex` (SMK's ISP bootloader) and
`nuphy-air60_default_full.hex` (bootloader plus firmware), and
`nuphy-air60_default_full_flash` writes that over ICP with
[sinodude](https://github.com/karstx/sinodude). See
[docs/bootloader.md](docs/bootloader.md).

### Debug Console

Debug builds ship their log output as HID reports. `smk-console` ([tools/smk-console](tools/smk-console)) prints them, and runs on Linux, macOS and Windows:

```sh
cargo run --release --manifest-path tools/smk-console/Cargo.toml               # defaults to 05ac:024f (nuphy-air60)
cargo run --release --manifest-path tools/smk-console/Cargo.toml -- 258a:002a  # eyooso-z11
cargo run --release --manifest-path tools/smk-console/Cargo.toml -- 258a:001f  # genesis-thor-300
cargo run --release --manifest-path tools/smk-console/Cargo.toml -- 258a:0016  # ciy-x77
```

It picks devices up and drops them as they are plugged and unplugged, so it can be left running across a reflash. On Linux the `/dev/hidraw*` node needs a udev rule, or `sudo`.

## Acknowledgements

* [libfx2](https://github.com/whitequark/libfx2)
* [LUFA](https://github.com/abcminiuser/lufa)
* [QMK](https://github.com/qmk/qmk_firmware)
