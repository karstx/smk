#pragma once
// ISY IGM3000 (SH68F881) board definitions, recovered from the stock firmware.
// Addresses in comments refer to the stock image (igm3000_full_32k_jtag.bin).

#include "sfr.h"
#include <stdbool.h>
#include <stdint.h>

#define FW_VERSION 0x04

// USB identity: VID:PID kept identical to stock so unmodified sinowisp finds the
// mouse with --vendor_id 0x258a --product_id 0x1007 (ISP report on interface 1).
#define USB_VID              0x258a
#define USB_PID              0x1007
#define USB_STR_MANUFACTURER "ISY"
#define USB_STR_PRODUCT      "IGM3000 Gaming Mouse"
#define USB_STR_SERIAL       "0001"

// Safety: jump to the ISP bootloader if USB never reaches CONFIGURED within this
// many milliseconds after power-on. 0 disables the fallback.
#ifndef ENUM_FALLBACK_MS
#    define ENUM_FALLBACK_MS 10000u
#endif

// Holding at least this many buttons while plugging in enters the bootloader.
#define BOOT_CHORD_MIN_BUTTONS 2

// Button bit (see input.c) -> action. Verified on the device 2026-11-19:
// bit0 Left, bit1 Right, bit2 Middle, bit3 G4, bit4 G5, bit5 DPI+, bit6 DPI-, bit7 G7.
enum button_action {
    ACT_NONE = 0,
    ACT_LEFT,
    ACT_RIGHT,
    ACT_MIDDLE,
    ACT_BACK,
    ACT_FORWARD,
    ACT_DPI_UP,
    ACT_DPI_DOWN,
    ACT_LED_MODE,
};

#define BUTTON_MAP_INIT {ACT_LEFT, ACT_RIGHT, ACT_MIDDLE, ACT_BACK, ACT_FORWARD, ACT_DPI_UP, ACT_DPI_DOWN, ACT_LED_MODE}

// Timebase: Timer2 interrupt every 0.5 ms (2 MHz timer clock, reload 0xFC18).
extern volatile uint16_t ms_ticks; // milliseconds since boot (wraps)
extern volatile __bit    ms_flag;  // set every ms, cleared by main loop

#ifndef NOP
#    define NOP() __asm nop __endasm
#endif
