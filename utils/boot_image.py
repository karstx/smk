#!/usr/bin/env python3
"""Merge an application and a bootloader into one full-flash image.

This is the layout a programmer writes over ICP, and the one the bootloader
maintains from then on: reset jumps to the bootloader, and the application's
own reset vector lives at `firmware_size - 5` where the bootloader looks for
it. It is `sinowisp convert --direction to_jtag` with the bootloader spliced in.

Usage: boot_image.py APP_HEX BOOT_HEX FLASH_SIZE BOOTLOADER_SIZE
"""

import sys

BLANK = 0x00
LJMP = 0x02


def read_ihex(path):
    cells = {}
    offset = 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        count = int(line[1:3], 16)
        address = int(line[3:7], 16)
        record = int(line[7:9], 16)
        data = bytes.fromhex(line[9 : 9 + count * 2])
        if record == 0x04:
            offset = int.from_bytes(data, "big") << 16
        elif record == 0x00:
            for i, byte in enumerate(data):
                cells[offset + address + i] = byte
    return cells


def write_ihex(image, out):
    # every byte of the flash, blanks included: a programmer that pads a gap with
    # 0xff rather than the 0x00 an erase leaves would write the wrong thing
    for base in range(0, len(image), 16):
        chunk = image[base : base + 16]
        record = [len(chunk), (base >> 8) & 0xFF, base & 0xFF, 0x00, *chunk]
        checksum = (-sum(record)) & 0xFF
        out.write(":" + bytes(record + [checksum]).hex().upper() + "\n")
    out.write(":00000001FF\n")


def main():
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)

    app_hex, boot_hex = sys.argv[1], sys.argv[2]
    flash_size, bootloader_size = int(sys.argv[3]), int(sys.argv[4])

    firmware_size = flash_size - bootloader_size
    app_vector = firmware_size - 5
    isp_entry = flash_size - 0x100

    app = read_ihex(app_hex)
    boot = read_ihex(boot_hex)

    if app.get(0x0000) != LJMP:
        raise SystemExit(f"{app_hex}: does not start with an LJMP reset vector")
    beyond = [a for a in app if a >= app_vector]
    if beyond:
        raise SystemExit(f"{app_hex}: reaches {max(beyond):#06x}, past the boot marker at {app_vector:#06x}")

    stray = [a for a in boot if not firmware_size <= a < flash_size]
    if stray:
        raise SystemExit(f"{boot_hex}: has code at {min(stray):#06x}, outside the bootloader region")
    for required, what in ((firmware_size, "entry point"), (isp_entry, "ISP entry stub")):
        if required not in boot:
            raise SystemExit(f"{boot_hex}: nothing at {required:#06x}, the {what}")

    image = bytearray([BLANK]) * flash_size
    for address, byte in app.items():
        image[address] = byte
    for address, byte in boot.items():
        image[address] = byte

    entry = bytes([app[0x0001], app[0x0002]])
    image[0x0000:0x0003] = bytes([LJMP, firmware_size >> 8, firmware_size & 0xFF])
    image[app_vector : app_vector + 3] = bytes([LJMP]) + entry

    write_ihex(image, sys.stdout)


if __name__ == "__main__":
    main()
