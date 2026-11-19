#pragma once
#include <stdint.h>

extern uint8_t buttons_raw;
extern uint8_t buttons_stable;
extern int8_t  wheel_accum;
extern int8_t  wheel_total;

void    input_init(void);
uint8_t buttons_scan_raw(void);
uint8_t buttons_task(void);
void    wheel_task(void);

// Keep interrupts off while SFR page 1 is selected so no ISR runs on the wrong page.
#define EA_SAVE_DISABLE()   \
    uint8_t _ea_saved = EA; \
    EA                = 0
#define EA_RESTORE() EA = _ea_saved
