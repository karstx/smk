#include "matrix.h"
#include "report.h"
#include "debug.h"
#include "layout.h"
#include "user_layout.h"
#include "kb.h"
#include "user_matrix.h"
#include "kbdef.h"
#include "host.h"
#include "delay.h"
#include "indicators.h"
#include "sleep.h"
#include <stdlib.h>
#include <stdbool.h>

typedef uint8_t matrix_col_t;

matrix_col_t matrix[MATRIX_COLS];
matrix_col_t matrix_previous[MATRIX_COLS];

#ifdef MATRIX_DEBOUNCE_SCANS
// eager, per key: a change is reported on the first sweep that sees it, then that
// key alone ignores its contacts for MATRIX_DEBOUNCE_SCANS sweeps while they bounce
static uint8_t      debounce_lock[MATRIX_COLS][MATRIX_ROWS]; // sweeps left per key
static matrix_col_t debounce_locked[MATRIX_COLS];            // keys with a lock running

static void debounce_age(uint8_t col, matrix_col_t changed)
{
    if (!(debounce_locked[col] | changed)) {
        return;
    }

    matrix_col_t row_mask = 1;
    for (uint8_t row = 0; row < MATRIX_ROWS; row++, row_mask <<= 1) {
        if (changed & row_mask) {
            debounce_lock[col][row] = MATRIX_DEBOUNCE_SCANS;
            debounce_locked[col] |= row_mask;
        } else if ((debounce_locked[col] & row_mask) && !--debounce_lock[col][row]) {
            debounce_locked[col] &= (matrix_col_t)~row_mask;
        }
    }
}
#endif

volatile bool matrix_updated;

#ifdef MATRIX_EVENT_QUEUE
// The scan ISR logs every accepted key change here in the order it saw them, and
// matrix_task replays them. Diffing a snapshot instead loses ordering whenever the
// main loop is slow: two changes landing in one gap get processed in column order,
// so e.g. a Shift release (C0) beats the letter pressed before it.
#    define EVQ_MASK (MATRIX_EVENT_QUEUE - 1)
_Static_assert((MATRIX_EVENT_QUEUE & EVQ_MASK) == 0, "MATRIX_EVENT_QUEUE must be a power of two");
_Static_assert(MATRIX_COLS <= 32 && MATRIX_ROWS <= 8, "event encoding is col << 3 | row");

static uint8_t          evq_key[MATRIX_EVENT_QUEUE]; // col << 3 | row
static bool             evq_pressed[MATRIX_EVENT_QUEUE];
static volatile uint8_t evq_head; // written by the scan ISR
static volatile uint8_t evq_tail; // written by matrix_task
static volatile bool    evq_overflow;

static void evq_push(uint8_t col, matrix_col_t changed, matrix_col_t state)
{
    matrix_col_t row_mask = 1;
    for (uint8_t row = 0; row < MATRIX_ROWS; row++, row_mask <<= 1) {
        if (!(changed & row_mask)) {
            continue;
        }
        const uint8_t next = (uint8_t)((evq_head + 1) & EVQ_MASK);
        if (next == evq_tail) {
            evq_overflow = true; // matrix_task resyncs from matrix[]
            return;
        }
        evq_key[evq_head]     = (uint8_t)((col << 3) | row);
        evq_pressed[evq_head] = (state & row_mask) != 0;
        evq_head              = next;
    }
}
#endif

uint8_t action_layer;

uint8_t default_layer;

void matrix_init()
{
    action_layer   = 0;
    default_layer  = 0;
    matrix_updated = false;

    for (int i = 0; i < MATRIX_COLS; i++) {
        matrix[i]          = 0;
        matrix_previous[i] = 0;
#ifdef MATRIX_DEBOUNCE_SCANS
        debounce_locked[i] = 0;
#endif
    }
}

void set_default_layer(uint8_t layer)
{
    default_layer = layer;
}

static uint16_t resolve_keycode(uint16_t base, uint8_t row, uint8_t col)
{
    if (!action_layer) {
        return base;
    }

    const uint16_t overlay = keymaps[action_layer][row][col];
    return (overlay == KC_TRANSPARENT) ? base : overlay;
}

static void send_keycode(uint16_t qcode, bool pressed)
{
    if (IS_MODIFIER_KEYCODE(qcode)) {
        if (pressed) {
            add_mods(MOD_BIT((uint8_t)(qcode & 0xFF)));
        } else {
            del_mods(MOD_BIT((uint8_t)(qcode & 0xFF)));
        }
        send_keyboard_report();
        return;
    }

    if (IS_BASIC_KEYCODE(qcode)) {
        if (pressed) {
            add_key((uint8_t)(qcode & 0xFF));
        } else {
            del_key((uint8_t)(qcode & 0xFF));
        }
        send_keyboard_report();
        return;
    }

    if (IS_SYSTEM_KEYCODE(qcode)) {
        host_system_send(pressed ? keycode_to_system(qcode) : 0);
        return;
    }

    if (IS_CONSUMER_KEYCODE(qcode)) {
        host_consumer_send(pressed ? keycode_to_consumer(qcode) : 0);
        return;
    }
}

static void process_key_state(uint8_t row, uint8_t col, bool pressed)
{
    indicators_key_event(row, col, pressed);

    const uint16_t base = keymaps[default_layer][row][col];

    if (IS_QK_MOMENTARY(base)) {
        if (pressed) {
            action_layer = QK_MOMENTARY_GET_LAYER(base);
        } else {
            clear_keys();
            action_layer = 0;
        }
        return;
    }

    const uint16_t qcode = resolve_keycode(base, row, col);

    if (!kb_process_record(qcode, pressed)) {
        return;
    }

    if (!layout_process_record(qcode, pressed)) {
        return;
    }

    send_keycode(qcode, pressed);
}

void matrix_scan_full(void)
{
    indicators_pwm_disable();

    user_matrix_sinks_off();

    user_matrix_scan_pre();
    user_matrix_cols_deselect_all();

    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        user_matrix_col_select(col);

        delay_us(10); // let the row lines settle before sampling
        const uint8_t sample1 = user_matrix_read_rows();
        delay_us(10);
        const uint8_t sample2 = user_matrix_read_rows();

#if defined(MATRIX_DEBOUNCE_SCANS) || defined(MATRIX_EVENT_QUEUE)
        matrix_col_t changed = 0;
        if (sample1 == sample2) {
            changed = (matrix_col_t)((matrix_col_t)~sample1 ^ matrix[col]);
#    ifdef MATRIX_DEBOUNCE_SCANS
            changed &= (matrix_col_t)~debounce_locked[col];
#    endif
            matrix[col] ^= changed;
        }
#    ifdef MATRIX_DEBOUNCE_SCANS
        debounce_age(col, changed);
#    endif
#    ifdef MATRIX_EVENT_QUEUE
        if (changed) {
            evq_push(col, changed, matrix[col]);
        }
#    endif
#else
        if (sample1 == sample2) {
            matrix[col] = ~sample1;
        }
#endif

        user_matrix_col_deselect(col);
    }

    user_matrix_scan_post();

    indicators_pwm_enable();

    matrix_updated = true;
}

uint8_t matrix_task()
{
#ifdef MATRIX_EVENT_QUEUE
    bool replayed = false;
    while (evq_tail != evq_head) {
        const uint8_t key     = evq_key[evq_tail];
        const bool    pressed = evq_pressed[evq_tail];
        evq_tail              = (uint8_t)((evq_tail + 1) & EVQ_MASK);

        const uint8_t      col      = key >> 3;
        const uint8_t      row      = key & 0x07;
        const matrix_col_t row_mask = (matrix_col_t)(1 << row);

        sleep_note_activity();
        process_key_state(row, col, pressed);
        if (pressed) {
            matrix_previous[col] |= row_mask;
        } else {
            matrix_previous[col] &= (matrix_col_t)~row_mask;
        }
        replayed = true;
    }

    if (!evq_overflow) {
        matrix_updated = false;
        return replayed;
    }
    evq_overflow = false; // queue overflowed: fall through and diff against matrix[]
#else
    if (!matrix_updated) {
        return false;
    }
#endif

    // Snapshot the scan-written matrix[], then diff it against
    // matrix_previous[]. No lock needed: each column byte reads atomically, so a
    // concurrent scan lands cleanly on one side of the read - at worst a
    // transition is split across two main-loop iterations, never lost.
    matrix_col_t snapshot[MATRIX_COLS];
    matrix_updated = false;
    for (uint8_t i = 0; i < MATRIX_COLS; i++) {
        snapshot[i] = matrix[i];
    }

    bool matrix_changed = false;

    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        const matrix_col_t current_col = snapshot[col];
        const matrix_col_t col_changes = current_col ^ matrix_previous[col];
        if (!col_changes) {
            continue;
        }
        matrix_changed = true;
        sleep_note_activity(); // a key changed state; reset the inactivity timer

        matrix_col_t row_mask = 1;
        for (uint8_t row = 0; row < MATRIX_ROWS; row++, row_mask <<= 1) {
            if (col_changes & row_mask) {
                const bool key_pressed = current_col & row_mask;
                process_key_state(row, col, key_pressed);
            }
        }

        matrix_previous[col] = current_col;
    }

    return matrix_changed;
}
