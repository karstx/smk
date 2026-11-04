# VIA and Dynamic Keymap Subsystem

SMK supports on-the-fly key remapping and dynamic keymaps using the [VIA](https://caniusevia.com/) protocol over USB Raw HID.

## Overview

Historically, changing key bindings on SMK required modifying `layout.c` and recompiling the firmware. The Dynamic Keymap Subsystem provides:

- **VIA v2 / v3 protocol implementation** over standard USB Raw HID (`0xFF60:0x61`, 32-byte reports).
- **Persistent flash storage** with sector validation, wear reduction, and power-loss safety.
- **Zero cost when disabled**: completely gated behind `-Dvia=disabled` (default), ensuring no flash or RAM footprint on unmodified configurations.

## Enabling VIA

To enable VIA and dynamic keymaps, configure the build with `-Dvia=enabled`:

```sh
meson setup build -Dvia=enabled
ninja -C build <keyboard>_default_smk.hex
```

When enabled on platforms using flash settings storage:
- Compiler options `-DVIA_ENABLE=1` and `-DDYNAMIC_KEYMAP_ENABLE=1` are activated.
- Dynamic keymap storage sectors are reserved in flash immediately below the user settings sector.
- Firmware `--code-size` limit is adjusted automatically to protect the keymap flash region.
- Endpoint 2 adds a bidirectional Raw HID interface for host communication.

## Flash Storage & Reliability

The dynamic keymap is stored in reserved flash sectors with built-in data integrity protections:

- **Magic header and version check**: Verifies sector boundaries and schema compatibility.
- **Checksum validation**: Detects partial or corrupt sector state across all layers.
- **Two-phase atomic commit**: Sector payload is written with commit byte erased (`0xFF`); the commit marker (`0xA5`) is programmed strictly last. If power is interrupted mid-write, the firmware detects an uncommitted state and restores cleanly from default compiled layout tables.
- **Write-coalescing**: Changes received over VIA are debounced in RAM for 1 second before triggering a flash erase/write cycle, preventing unnecessary flash wear during rapid key configuration.
