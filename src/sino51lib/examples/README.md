# sino51lib examples

Standalone firmware that uses the library and nothing else. Each one lists exactly the
modules it links, so a dependency that creeps from the library into the rest of the tree
shows up here as a link error rather than years later.

Build them all:

```sh
meson compile -C build examples
```

which leaves `ex-<example>-<part>.hex` in the build directory. They are not installed and no
keyboard uses them.

| example | parts | exercises |
| --- | --- | --- |
| `gpio_blink` | all | `reset_init`, `ldo_init`, `clock_init`, `peripherals_init`, the GPIO macros, `delay_ms`, `watchdog_kick` |
| `uart_hello` | all | the EUART: baud from `FREQ_SYS`, `uart_putc`, the transmit-complete ISR |
| `spi_xfer` | all | hardware SPI master, full duplex, divider checked against the part's SPR ladder |
| `twi_probe` | sh68f89 | hardware TWI master: START, address, STOP, walking the address space |
| `pwm_duty` | sh68f90 | the PWM banks through `pwm.h`'s duty macros |
| `pca_duty` | sh68f89 | the PCA units as an 8-bit PWM bank, with `pca_hold`/`pca_release` around a reload |
| `flash_info` | all | reading the factory information block through the FAC window |
| `usb_minimal` | all | `usbhw.c`: the SIE up, EP0 answered well enough to enumerate, EP1 and EP2 IN |
| `systick_tick` | all | `systick.c`: Timer2 as the periodic tick, both slot lengths |
| `bb_i2c_probe` | all | the bit-banged I2C master, on two GPIO pins named in `kbdef.h` |
| `isp_powerdown` | all | `power_enter_powerdown()`, the `extint` wake calls, `isp_jump()` |
| `boot_minimal` | all | the ISP bootloader in `boot/`, with empty entry hooks; built as `ex-boot_minimal-<part>_boot.hex` at the top of flash |

Each application example links six to eight modules and comes to under two kilobytes.

## Coverage

The point of these is that they actuate the library, not that they demonstrate it, so the
whole callable surface is reached: of the 95 functions and function-like macros the library's
headers declare, 88 are called by an example and the rest are macros expanded inside other
macros in the same header. `utils` has no script for this; it was checked by hand against
`src/sino51lib/**/*.h`.

Two things are deliberately not exercised. `flash_program_from()` and `flash_erase()` are
reachable from `flash_info` but left alone: an erase aimed at the wrong sector takes out the
running code, and the settings store in `src/nvm/flash` is where that path belongs. And
`isp_powerdown` is build-only -- see the warning at the top of it.

## Hooks

Two of the library's modules call outwards rather than only being called:

| module | wants | supplied by |
| --- | --- | --- |
| `usbhw.c` | `usb_irq_dispatch()` | the firmware's USB device stack, or `usb_minimal/main.c` |
| `systick.c` | `tick_dispatch()` | the firmware's scheduler, or `systick_tick/main.c` |
| `power.c` | `usb_init()`, `usb_deinit()`, `usb_suspended` | the firmware, or stubs in `isp_powerdown/main.c` |

Each is behind a header the user of the library owns. `src/smk` has its own `usb.h` and
`tick.h`; the examples have their own, a few lines each, and link none of the firmware.

`power.c`'s is the widest of the three and the least like a hook: bringing a USB device back
up after the clock tree restarts is the application's job, so the example stubs it and sleeps
with nothing attached.

## Pins and board configuration

The on-chip blocks sit on pins the part fixes, not pins a board chooses: see the part's
`spi_hw.h`, `twi_hw.h` and `uart_hw.h`, and the tables in `docs/platforms/`. `gpio_blink`
picks P3.0 arbitrarily and says so.

Where a driver reads its configuration from `kbdef.h` — the SPI clock divider, the TWI bit
rate — the example ships a `kbdef.h` of its own holding what a board would set.

## What is not here, and why

`diag.c` prints through `dprintf`, so it belongs to whatever owns the debug output; it is the
one module left that an application cannot take without `src/smk`.

`bb_spi.c` used to live in the library and does not any more: it drives the BK3632's MOT and
CS lines by name and waits on that module's ACK, so it is a transport for one peripheral
rather than a general bit-banged master. It is in `src/peripherals/bk3632/` with its
consumer. `bb_i2c.c` is the genuinely general one, and `bb_i2c_probe` exercises it.

Nothing else in the library reaches into `src/smk`, and the examples are built with `src/smk`
off the include path, so anything that starts to will fail here rather than quietly compiling.
