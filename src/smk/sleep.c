#include "sleep.h"

#ifndef SLEEP_ENABLE
typedef int sleep_disabled_placeholder_t;
#else

#    include "indicators.h"
#    include "tick.h"
#    include "power.h"
#    include "usb.h"
#    include "user_sleep.h"
#    include "kbdef.h"
#    ifdef RF_ENABLED
#        include "rf_controller.h"
#        include "keyboard.h"
#        include "delay.h"
#    endif

#    include <stdbool.h>
#    include <stdint.h>

#    ifndef SLEEP_TIMEOUT
#        define SLEEP_TIMEOUT 21000 // LED frames of inactivity; boards may override in kbdef.h
#    endif

// `inactivity` is owned by sleep_note_frame(); the main loop only ever reads it
// indirectly via `sleep_requested`. Activity reaches the tick through the
// single-byte `activity_seen` flag, so there is no read-modify-write race on the
// 16-bit counter across contexts.
static volatile uint16_t inactivity;
static volatile uint8_t  activity_seen;
static volatile uint8_t  sleep_requested;

void sleep_init(void)
{
    inactivity      = 0;
    activity_seen   = 0;
    sleep_requested = 0;
}

void sleep_note_frame(bool frame_completed)
{
    if (!frame_completed) {
        return;
    }

    if (activity_seen) {
        activity_seen   = 0;
        inactivity      = 0;
        sleep_requested = 0;
        return;
    }

    if (inactivity < SLEEP_TIMEOUT) {
        if (++inactivity >= SLEEP_TIMEOUT) {
            sleep_requested = 1;
        }
    }
}

void sleep_note_activity(void)
{
    activity_seen = 1;
}

static bool sleep_due(user_sleep_mode_t mode)
{
    switch (mode) {
        case USER_SLEEP_RF:
            return sleep_requested;
        case USER_SLEEP_USB:
            return usb_suspended;
        default:
            return false;
    }
}

#    ifdef RF_ENABLED
#        define RF_RESYNC_TRIES 10

static void rf_resync_after_wake(void)
{
    static uint8_t tries;

    for (tries = RF_RESYNC_TRIES; tries > 0; tries--) {
        rf_wake_nudge();
        delay_ms(10);
        if (rf_update_keyboard_state(&keyboard_state) && keyboard_state.connected) {
            return;
        }
        delay_ms(10);
    }
}
#    endif

void sleep_task(void)
{
    const user_sleep_mode_t mode = user_sleep_supported(); // tracks the conn slider live

    if (!sleep_due(mode)) {
        if (mode != USER_SLEEP_RF) {
            sleep_note_activity();
        }
        return;
    }

    sleep_requested = 0;

    const powerdown_mode_t powerdown = (mode == USER_SLEEP_USB) ? POWERDOWN_KEEP_USB_ALIVE : POWERDOWN_RELEASE_USB;

#    ifdef RF_ENABLED
    if (powerdown == POWERDOWN_RELEASE_USB) {
        rf_wake();
    }
#    endif

    tick_pause();
    indicators_pwm_disable();

    user_sleep_prepare(); // parks the board's hardware and arms the wake source
    power_enter_powerdown(powerdown);
    user_sleep_wake();

    indicators_pwm_enable();
    tick_resume();

#    ifdef RF_ENABLED
    if (powerdown == POWERDOWN_RELEASE_USB) {
        rf_resync_after_wake();
    }
#    endif

    sleep_note_activity();
}

#endif // SLEEP_ENABLE
