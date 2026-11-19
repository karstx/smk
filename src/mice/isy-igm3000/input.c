// Buttons and scroll wheel, ported from stock 0x0FC5 (buttons) and 0x372B (wheel).
#include "input.h"
#include "board.h"
#include "delay.h"

uint8_t buttons_raw;    // 1 = pressed, bit layout below
uint8_t buttons_stable; // debounced
int8_t  wheel_accum;    // detents since last report
int8_t  wheel_total;    // running total (diag)

static uint8_t last_raw;
static uint8_t same_count;
static uint8_t wheel_state;

void input_init(void)
{
    sfr_page_0();
    // P3.0 is the matrix column: park it high before making it an output.
    P3_0 = 1;
    P3CR |= 0x01;
    // P2.1..P2.3 inputs with pull-ups (pull-ups set in gpio_init)
    P2CR &= 0xf1;
    // wheel: P0.6/P0.7 inputs with pull-ups
    P0CR &= 0x3f;
    P0PCR |= 0xc0;
    // P6.4..P6.7 inputs (page 1)
    sfr_page_1();
    P6CR &= 0x0f;
    sfr_page_0();

    wheel_state = (P0_7 ? 1 : 0) | (P0_6 ? 2 : 0);
    buttons_raw = buttons_stable = last_raw = 0;
    same_count                              = 0;
}

// Bits: 0=P2.1 1=P2.2 2=P2.3 3=P6.6 4=P6.7 (direct to ground),
//       5=P2.1 6=P2.2 7=P2.3 via P3.0 low (only scanned when bits 0..4 idle).
uint8_t buttons_scan_raw(void)
{
    uint8_t v = 0;
    uint8_t p66, p67;

    sfr_page_0();
    P3_0 = 1;
    NOP();
    NOP();
    if (!P2_1) v |= 0x01;
    if (!P2_2) v |= 0x02;
    if (!P2_3) v |= 0x04;

    EA_SAVE_DISABLE();
    sfr_page_1();
    p66 = P1_6; // P6.6 at the same address on page 1
    p67 = P1_7;
    sfr_page_0();
    EA_RESTORE();

    if (!p66) v |= 0x08;
    if (!p67) v |= 0x10;

    if (v == 0) {
        P3_0 = 0;
        NOP();
        NOP();
        NOP();
        NOP();
        if (!P2_1) v |= 0x20;
        if (!P2_2) v |= 0x40;
        if (!P2_3) v |= 0x80;
        P3_0 = 1;
    }
    return v;
}

// Call once per ms. Returns 1 when the debounced state changed.
uint8_t buttons_task(void)
{
    uint8_t v   = buttons_scan_raw();
    buttons_raw = v;
    if (v == last_raw) {
        if (same_count < 255) {
            same_count++;
        }
    } else {
        last_raw   = v;
        same_count = 0;
    }
    if (same_count >= 3 && buttons_stable != v) {
        buttons_stable = v;
        return 1;
    }
    return 0;
}

// Call as often as possible. 6-bit transition history like stock:
// 0x07 / 0x38 -> +1, 0x0B / 0x34 -> -1 (detents at 00 and 11).
void wheel_task(void)
{
    sfr_page_0();
    uint8_t cur = (P0_7 ? 1 : 0) | (P0_6 ? 2 : 0);
    if (cur == (wheel_state & 3)) {
        return;
    }
    wheel_state = ((wheel_state << 2) & 0x3c) | cur;
    switch (wheel_state) {
        case 0x07:
        case 0x38:
            if (wheel_accum < 127) wheel_accum++;
            wheel_total++;
            break;
        case 0x0b:
        case 0x34:
            if (wheel_accum > -127) wheel_accum--;
            wheel_total--;
            break;
        default:
            break;
    }
}
