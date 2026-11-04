#include "tick.h"
#include "systick.h"
#include "matrix.h"
#include "dynamic_keymap.h"
#include "indicators.h"
#include "keyboard.h"
#include "sleep.h"
#include "kbdef.h"
#include <stdbool.h>
#include <stdint.h>

// subframes must outnumber the scans, or the refresh rate is the scan rate divided by the subframe count.
#ifndef LED_SUBFRAMES_PER_SCAN
#    define LED_SUBFRAMES_PER_SCAN 1
#endif

static volatile bool    scan_due;
static volatile uint8_t subframes_since_scan;

void tick_init(void)
{
    scan_due             = true;
    subframes_since_scan = 0;

    systick_init();
}

static void run_matrix_scan(void)
{
    systick_arm(SYSTICK_SLOT_MATRIX_SCAN);
    matrix_scan_full();
}

static void run_led_subframe(void)
{
    systick_arm(SYSTICK_SLOT_LED_SUBFRAME);

    indicators_pre_update();
    const bool frame_wrapped = indicators_update_step(&keyboard_state, 0);
    indicators_post_update();

    sleep_note_frame(frame_wrapped);
}

void tick_dispatch(void)
{
#ifdef DYNAMIC_KEYMAP_ENABLE
    dynamic_keymap_tick();
#endif
    if (scan_due) {
        scan_due             = false;
        subframes_since_scan = 0;
        run_matrix_scan();
        return;
    }

    run_led_subframe();

    if (++subframes_since_scan >= LED_SUBFRAMES_PER_SCAN) {
        scan_due = true;
    }
}

void tick_pause(void)
{
    systick_pause();
}

void tick_resume(void)
{
    systick_resume();
}
