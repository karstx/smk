#!/usr/bin/env bash
#
# Drive a USB GET_DESCRIPTOR(device) control transfer through SMK firmware in the
# patched uCsim simulator `ucsim-sh68f90` (flake package; on PATH in the dev
# shell). The patched sim registers the SH68F90 USB interrupt (vector 7) and a
# minimal SIE model that logs each EP0 IN packet the firmware produces.
#
#   nix develop        # brings ucsim_51 onto PATH
#   meson compile -C build nuphy-air60_default_smk.hex
#   tools/ucsim/sim-get-descriptor.sh build/nuphy-air60_default_smk.hex
#
# Expected: "[SIE] EP0 IN[0] 8 bytes: 12 01 00 02 00 00 00 08" -- the start of the
# device descriptor (bLength=18, DEVICE, bcdUSB 0x0200, bMaxPacketSize0=8).
#
# Note: this cold-injects after reset, so the firmware's C runtime has not zeroed
# its __xdata globals and the EP0 state machine is not primed for multi-packet
# continuation -- only the first IN packet is produced. Running past crt0/usb_init
# first is the next step for a full multi-packet transfer.
set -euo pipefail

FW="${1:-build/nuphy-air60_default_smk.hex}"

"${SMK_UCSIM:-ucsim-sh68f90}" -t 51 -S in=/dev/null <<EOF
file "${FW}"
reset
# park the CPU on a NOP sled so the injected interrupt has somewhere to return to
set mem rom 0x9000 0 0 0 0 0 0 0 0
pc 0x9000
set mem sfr 0xa8 0x80   ; # IE.EA   - global interrupt enable
set mem sfr 0xa9 0x01   ; # IEN1.EUSB - USB interrupt enable
# stage a GET_DESCRIPTOR(device, len=64) SETUP packet in EP0_OUT_BUF (0x1100)
set mem xram 0x1100 0x80 0x06 0x00 0x01 0x00 0x00 0x40 0x00
set mem sfr 0x92 0x10   ; # USBIF1.SETUPIF - request the USB interrupt
break 0x9007
run
quit
EOF
