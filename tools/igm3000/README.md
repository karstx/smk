# ISY IGM3000 tools

Python 3, standard library only.

## verify.py

Pre-flash checks for the mouse firmware, run on the built image:

```sh
python3 tools/igm3000/verify.py build/isy-igm3000_smk.ihx build/isy-igm3000_smk.map [--quick]
meson test -C build isy-igm3000_verify       # same, --quick
```

Static checks: the image stays below `0x6FFB`, the reset vector is an LJMP, `isp_jump`
matches the stock jump sequence byte for byte, the interrupt vectors point at the right
handlers, and nothing writes the flash-programming registers.

Emulated scenarios (`emu51.py`):

- a warm start from the bootloader with an interrupt still in service;
- two buttons held at power-on, one button held at power-on;
- no host at all, where the fallback must reach the bootloader after about 10 s;
- full enumeration with each of the four fake sensors: descriptors, strings, the
  sinowisp feature report, diag report, motion, buttons, DPI LEDs, and the ISP command.

`--quick` runs one sensor type and skips the long post-configuration wait.

## emu51.py

A small 8051 emulator with just enough SH68F881 to run the firmware: SFR paging, Timer2
at FREQ_SYS/6, the USB block's registers and endpoint buffers, port pins with pull-ups,
a bit-banged SPI sensor model and a fake host. Timing is approximate. It models the
hardware from the register names and what was measured on the mouse, so it complements
testing on the device rather than replacing it.

## dis51.py

The recursive-descent disassembler used to read the stock firmware, with SH68F881
register names taken from `src/sino51lib/sh68f881/sh68f881.h`:

```sh
python3 tools/igm3000/dis51.py stock.bin stock.lst 7000 26c0 03 1b 43 4b 53 5b
```

Use the JTAG-layout image (`sinowisp convert --direction to_jtag`) so the reset vector
is real.

## igm3000-diag.py

Reads the firmware's diag feature report (sensor type and ID, raw and debounced
buttons, wheel, DPI, LED state, uptime) and sends test commands, over Linux hidraw:

```sh
sudo python3 tools/igm3000/igm3000-diag.py           # once
sudo python3 tools/igm3000/igm3000-diag.py watch     # on every button/wheel/DPI change
sudo python3 tools/igm3000/igm3000-diag.py led 255 0 0
sudo python3 tools/igm3000/igm3000-diag.py led off
```
