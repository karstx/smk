# SMK ISP Bootloader

A replacement for the ISP bootloader in the top 4 KiB of a SinoWealth 8051
keyboard's flash. It speaks the same HID protocol the stock one does, so
[sinowisp](https://github.com/karstx/sinowisp) drives it unchanged, and it
adds the one thing the stock bootloader has no way to offer: a key chord that
gets you into ISP mode from a cold start, whatever the firmware above it is
doing.

## Getting into ISP mode

Hold **S + M + K** while plugging the board in. The bootloader scans the matrix
before it starts anything else, and stays in ISP mode as long as the chord is
held for the first ~60 ms.

It also enters ISP mode when:

- the running firmware asks it to — `sinowisp write` does this for you, and it
  is what SMK's `isp_jump()` does; or
- there is no application to run, which is the state an interrupted write
  leaves the board in.

Otherwise it starts the application immediately; the chord scan costs about
60 ms of boot time and nothing else.

The chord is resolved per board out of its default layout, so S, M and K are
the keys with those letters on them wherever they sit in the matrix. Change it
with `-Dboot_chord=`, naming keys without the `KC_` prefix:

```sh
meson configure build -Dboot_chord=Q,W,E
```

A key the board's layout does not have fails the build rather than silently
producing a bootloader you cannot reach.

## What it fixes

The stock bootloaders enumerate badly, in ways that make a flashing tool work
around them rather than just talk to them:

| stock | here |
| --- | --- |
| `bNumEndpoints = 0` on an HID interface, which HID 1.11 §4.4 forbids | one interrupt IN endpoint, with an input report to go with it |
| two top-level application collections, which Windows splits into two HID device paths — so a tool has to find and open both halves | one collection carrying both reports |
| `Logical Maximum` written `25 FF`, which is signed −1 against a minimum of 0 | `26 FF 00`, which is 255 |
| no string descriptors, and one VID:PID shared by every SinoWealth board in ISP mode | manufacturer, product and serial strings; the product names the board and the serial carries the VID:PID its application runs under |
| `bcdUSB 1.10` | `bcdUSB 2.00` |
| no way in without working firmware above it | the chord, before anything else runs |

What it deliberately keeps is the identity and the wire protocol: VID:PID
`0603:1020`, interface 0, feature report 5 for commands and 6 for page
transfers, and the entry point at `ISP_ENTRY` that `isp_jump()` uses. Existing
tools need no changes.

## Installing it

The bootloader cannot install itself: it refuses writes to its own region, and
so does the stock bootloader, so the first one has to go in over ICP. With an
Arduino Nano running
[sinodude-serial](https://github.com/karstx/sinodude/tree/main/firmware)
wired to the board:

```sh
meson compile -C build nuphy-air60_default_full_flash
```

That builds the application and the bootloader stitched into one image, with
both reset vectors in place, and writes the whole code flash. The programmer's
serial port defaults to `/dev/ttyUSB0`:

```sh
meson configure build -Dicp_port=/dev/cu.usbserial-1420
```

To write through a SinoLink dongle instead:

```sh
meson configure build -Dicp_programmer=sinolink
```

`meson compile -C build nuphy-air60_default_full.hex` builds the image without
writing it, for ProWriter or any other programmer.

Code options are not touched — this writes code flash only, so the board keeps
the configuration it shipped with. **Dump your stock flash first**: this
overwrites the stock bootloader, and a full ICP read is the only way back to
the firmware the board came with.

Afterwards, `sinowisp write` updates the application alone, the way it always
did.

### Replacing only the bootloader

To swap the stock bootloader for this one and keep the firmware that is on the
board:

```sh
meson compile -C build nuphy-air60_boot_flash
```

This erases and writes the bootloader region alone, so it only works when the
application below it was written through an ISP bootloader (stock firmware,
or anything `sinowisp write` put there): `0x0000` has to jump into the
bootloader and the application's entry has to sit at `firmware_size - 5`, as
described under [Flash layout](#flash-layout). On a board whose application was
written over ICP as a plain image, use `_full_flash` instead, or the board
boots straight into the application and never reaches the bootloader.

## Flash layout

Reset lands at `0x0000`, so the application cannot own its own reset vector.

| where | what |
| --- | --- |
| `0x0000`–`0x0002` | `LJMP` into the bootloader. Written by the erase command, and skipped by every page write. |
| `0x0003` .. | the application: its interrupt vectors and code |
| `firmware_size - 5` | `LJMP <application entry>`, in the sector the build already reserves. Its presence is what marks the application bootable. |
| `firmware_size` .. | the bootloader |
| `flash_size - 0x100` | the entry stub, at `ISP_ENTRY` |

On an SH68F90 with 64 KiB of flash that is `0x0000`, `0xEFFB`, `0xF000` and
`0xFF00`.

The application's real reset target is moved to `firmware_size - 5` on the way
in, and reads of `0x0001`–`0x0002` are answered from there, which is what makes
a written image read back byte-identical. This is the layout `sinowisp convert`
calls the JTAG payload.

### Page data offsets

Some stock bootloaders offset the bytes of every page they transfer, and the
flashing tools apply the inverse for those boards. To keep those tools working
unchanged, a board can set `boot_stock_scramble` in its options, and the
bootloader then does the same: it subtracts `0x5A` from each byte it writes,
and adds `0x5E` to each byte it reads after the first six of a page. The CIY
X77 has it on, which is what `sinowisp -d ciy-x77` expects. On such a board,
driving the bootloader with `--platform` and no device preset garbles the
image; verification catches it before the image is enabled.

Two addresses in that table are an ABI with the application, which is linked
separately and cannot check them: `utils/boot_check.py` asserts both against
the linker map, and the build fails if either moves.

## Build targets

Per board, with the bootloader enabled (the default; `-Dboot=disabled` turns it
off):

- `<board>_boot.hex` — the bootloader alone, for the top of flash
- `<board>_<layout>_full.hex` — application plus bootloader, for a programmer
- `<board>_<layout>_full_flash` — writes that image over ICP with sinodude.
  Needs `icp_part` on the board (the name sinodude programs it as) and
  `sinodude` on `PATH`; without either, the target is simply absent.

## Design notes

**It polls USB.** The bootloader lives above the application but shares the
interrupt vectors at `0x0003` upwards with it, and those belong to the
application. Claiming none of them means the two never have to agree on
anything but the three addresses above. `EA` stays clear throughout.

**A page is streamed, not buffered.** The 2049-byte transfer report is far
larger than the 8-byte control buffer, and flash is programmed a byte at a time
as each control packet arrives, so no page buffer is needed and the main loop
keeps kicking the watchdog throughout.

**Erase and reboot are deferred.** Both are acknowledged on the control
transfer and run from the main loop, because erasing the whole application
region takes longer than a host will wait for a status stage.

## Using it outside SMK

The bootloader is part of sino51lib, in `src/sino51lib/boot/`, and nothing in
it depends on SMK. A firmware supplies two functions, declared in `boot.h`:

- `void boot_board_init(void)` runs once after the clock and peripherals are
  up. Put the board's pins in a safe state here.
- `bool boot_entry_held(void)` decides, on a cold start, whether to stay in
  ISP mode when a valid application is present. SMK scans its key chord here
  (`src/boot/chord.c`); a board without one can check a jumper, or return
  `false`.

The bootloader also enters ISP mode when the application jumps to the entry
stub with the ISP key, and when there is no valid application.

Its USB strings are build parameters: `BOOT_USB_MANUFACTURER`,
`BOOT_USB_PRODUCT` and `BOOT_USB_SERIAL`. Set the serial to the application's
VID:PID, because every board in ISP mode enumerates as `0603:1020` and the
serial is what tells them apart. `BOOT_STOCK_SCRAMBLE` turns on the page data
offsets described above.

`src/sino51lib/examples/boot_minimal` is a bootloader with empty hooks, built
for every supported part by `meson compile -C build examples`. The top-level
`meson.build` shows the full recipe: compile with the part's `boot_core`, link
at `flash_size - bootloader_size` with `--code-size` stopping short of the
entry stub, place the `BOOTISP` area at `flash_size - 0x100`, and check the
result with `utils/boot_check.py`.

## Adding a platform

A platform opts in by adding three entries to its spec in
`src/sino51lib/<part>/meson.build`:

- `boot_core` — the sources the bootloader needs from the platform (clock,
  peripherals, flash)
- `isp_page_size` and `ea_bit` — the page size the protocol uses, and the bit
  address of `EA` for the entry stub

An SMK board opts in with `boot_src` in its own `meson.build`, listing the
files the chord scan needs from it: its matrix and its GPIO setup.

The SH68F90, SH68F89 and SH68F881 are wired up. On parts with SFR paging,
`usb.c` selects page 1 around its USB register accesses and `ssp_run` selects
page 0 for each flash operation, restoring whatever page the caller had.

## Tests

`tests/test_boot_isp.py` and `tests/test_boot_chord.py` run the bootloader in
the patched uCsim simulator (`nix build .#ucsim-sh68f90`), which models the SIE
well enough to enumerate it and read a page back, and drives the key matrix at
pin level so the chord decision is the real scan. They run as part of
`meson test`.

The simulator's SIE stops feeding EP0 IN completions after 16 packets, so a
page read is checked over its first 120 bytes rather than all 2050.
