#include "boot.h"
#include "isp.h"
#include "usb.h"
#include BOOT_CHORD_HEADER
#include "kbdef.h"
#include "user_matrix.h"
#include "user_init.h"
#include "clock.h"
#include "ldo.h"
#include "peripherals.h"
#include "delay.h"
#include "watchdog.h"
#include "sfr.h"
#include <stdbool.h>
#include <stdint.h>

#define CHORD_SAMPLES   6u
#define CHORD_SAMPLE_MS 10u

_Static_assert(BOOT_CHORD_MAX_COL < MATRIX_COLS, "the chord names a column this board does not have");
_Static_assert(BOOT_CHORD_MAX_ROW < MATRIX_ROWS, "the chord names a row this board does not have");

static uint8_t matrix[MATRIX_COLS];

static void scan_matrix(void)
{
    user_matrix_sinks_off();
    user_matrix_scan_pre();
    user_matrix_cols_deselect_all();

    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        user_matrix_col_select(col);

        delay_us(10); // let the row lines settle before sampling
        const uint8_t first = user_matrix_read_rows();
        delay_us(10);
        const uint8_t second = user_matrix_read_rows();

        matrix[col] = (first == second) ? (uint8_t)~first : 0u;

        user_matrix_col_deselect(col);
    }

    user_matrix_scan_post();
}

static bool chord_down(void)
{
    static const __code uint8_t chord[BOOT_CHORD_COUNT][2] = BOOT_CHORD_KEYS;

    for (uint8_t key = 0; key < BOOT_CHORD_COUNT; key++) {
        if (!(matrix[chord[key][0]] & (uint8_t)(1u << chord[key][1]))) {
            return false;
        }
    }

    return true;
}

// held, not merely seen: a chord sampled once could be one scan of a key being released
static bool chord_held(void)
{
    for (uint8_t sample = 0; sample < CHORD_SAMPLES; sample++) {
        watchdog_kick();
        scan_matrix();

        if (!chord_down()) {
            return false;
        }

        delay_ms(CHORD_SAMPLE_MS);
    }

    return true;
}

void main(void)
{
    // the application can jump here with any SFR page or flash window selected
#ifdef sfr_page_0
    sfr_page_0();
#endif
    FLASHCON = 0;

    watchdog_kick();

    const bool requested = boot_isp_requested();

    ldo_init();
    clock_init();
    peripherals_init();

    user_init();

    // the bootloader polls USB so it never claims a vector; the application owns them all
    IEN0 = 0;
    IEN1 = 0;

    if (!requested && !chord_held() && boot_app_enabled()) {
        boot_enter_app();
    }

    isp_init();
    boot_usb_start();

    for (;;) {
        watchdog_kick();

        boot_usb_poll();
        isp_task();
    }
}
