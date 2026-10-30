#pragma once

#include <stdint.h>

typedef enum {
    POWERDOWN_KEEP_USB_ALIVE,
    POWERDOWN_RELEASE_USB,
} powerdown_mode_t;

// what ended the last power-down, for diagnostics: the INT4 flags (EXF1, one bit
// per wake pin; 0 = not INT4) and USBIF1 as they read on wake
extern volatile uint8_t power_wake_int4_flags;
extern volatile uint8_t power_wake_usbif1;

// the caller must arm a wake source first or the core never comes back.
void power_enter_powerdown(powerdown_mode_t mode);
