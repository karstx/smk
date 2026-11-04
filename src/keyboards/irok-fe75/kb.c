#include <stdint.h>
#include <stdbool.h>
#include "report.h"
#include "usb.h"
#include "kbdef.h"
#include "debug.h"
#include "keyboard.h"
#include "settings.h"

#ifdef RF_ENABLED
#    include "rf_controller.h"
#endif

extern void indicators_toggle();
extern void indicators_next_effect();
extern void indicators_brightness_up();
extern void indicators_brightness_down();
extern void indicators_speed_up();
extern void indicators_speed_down();

static bool gui_locked;

#define MODE_SW_DEBOUNCE_ITERS 256

// UNKNOWN until the first debounced read, so the slider's position is applied
// after main has brought the wireless chip up (rf_init, restore_rf_link).
mode_switch_t kb_mode_switch = MODE_SW_UNKNOWN;

void kb_init()
{
#ifdef RF_ENABLED
    // Stock's key packet builder (0x8BB5) always writes 0 to byte 9, right
    // after the five key slots. With the NuPhy's 1 in idle packets, the stock
    // 2.4G dongle woke a sleeping Windows PC within ~20 s.
    rf_byte9_set_disable(true);
#endif
}

#ifdef RF_ENABLED
#    define LINK_PAIRING_HOLD_TICKS 60000 // main-loop passes, ~3 s
static uint16_t link_hold_ticks;
static uint16_t link_hold_keycode;
static bool     link_pairing_armed;

static rf_mode_t kb_bt_link(void)
{
    const uint8_t link = user_settings.rf_link;
    return (link >= RF_MODE_BT1 && link <= RF_MODE_BT3) ? (rf_mode_t)link : RF_MODE_BT1;
}

static void kb_select_link(rf_mode_t link)
{
    rf_set_link(link);
    keyboard_state.rf_link   = (uint8_t)link;
    keyboard_state.connected = 1;
    keyboard_state.paired    = 1;
    rf_kbd_lazy_state_init();
}
#endif

static void kb_apply_mode(mode_switch_t mode)
{
    kb_mode_switch = mode;

    switch (mode) {
        case MODE_SW_USB:
            dprintf("USB_MODE\r\n");
#ifdef RF_ENABLED
            rf_apply_usb_mode();
#endif
            break;
        case MODE_SW_24G:
            dprintf("RF_24G_MODE\r\n");
#ifdef RF_ENABLED
            kb_select_link(RF_MODE_2_4G);
#endif
            break;
        case MODE_SW_BT:
            dprintf("RF_BT_MODE\r\n");
#ifdef RF_ENABLED
            kb_select_link(kb_bt_link());
#endif
            break;
        default:
            break;
    }
}

void kb_update_switches()
{
    static uint16_t debounce;

    const mode_switch_t raw = !MODE_SW_A ? MODE_SW_24G : (!MODE_SW_B ? MODE_SW_BT : MODE_SW_USB);
    if (raw == kb_mode_switch) {
        debounce = 0;
    } else if (++debounce >= MODE_SW_DEBOUNCE_ITERS) {
        debounce = 0;
        kb_apply_mode(raw);
    }
}

bool kb_process_record(uint16_t keycode, bool key_pressed)
{
    switch (keycode) {
        case GUI_LCK:
            if (key_pressed) {
                gui_locked = !gui_locked;
            }
            return false;
        case LED_TOG:
            if (key_pressed) {
                indicators_toggle();
            }
            return false;
        case BRI_UP:
            if (key_pressed) {
                indicators_brightness_up();
            }
            return false;
        case BRI_DN:
            if (key_pressed) {
                indicators_brightness_down();
            }
            return false;
        case SPD_DN:
            if (key_pressed) {
                indicators_speed_down();
            }
            return false;
        case SPD_UP:
            if (key_pressed) {
                indicators_speed_up();
            }
            return false;
        case FX_NEXT:
            if (key_pressed) {
                indicators_next_effect();
            }
            return false;
#ifdef RF_ENABLED
        case LNK_BT1:
        case LNK_BT2:
        case LNK_BT3:
        case LNK_24G: {
            // BT keys act in the BT slider position, LNK_24G in the 2.4G one
            const bool is_24g = (keycode == LNK_24G);
            if (kb_mode_switch != (is_24g ? MODE_SW_24G : MODE_SW_BT)) {
                return false;
            }
            if (key_pressed) {
                const rf_mode_t link = is_24g ? RF_MODE_2_4G : (rf_mode_t)(RF_MODE_BT1 + (keycode - LNK_BT1));
                dprintf("rf link selected %02x\r\n", link);
                kb_select_link(link);
                if (!is_24g) {
                    user_settings.rf_link = (uint8_t)link;
                    settings_mark_dirty();
                }
                link_hold_keycode  = keycode;
                link_hold_ticks    = 0;
                link_pairing_armed = true;
            } else if (link_hold_keycode == keycode) {
                link_hold_keycode  = 0;
                link_pairing_armed = false;
            }
            return false;
        }
#endif
        case KC_LGUI:
        case KC_RGUI:
            // swallow presses only, so a GUI held across the toggle still releases
            return !(gui_locked && key_pressed);
        default:
            return true;
    }
}

#ifdef RF_ENABLED
#    define KB_RF_ACTIVE() (kb_mode_switch == MODE_SW_24G || kb_mode_switch == MODE_SW_BT)
#else
#    define KB_RF_ACTIVE() false
#endif

void kb_send_report(__xdata report_keyboard_t *report)
{
#ifdef RF_ENABLED
    if (KB_RF_ACTIVE()) {
        rf_send_report(report);
        return;
    }
#endif
    usb_send_report(report);
}

void kb_send_nkro(__xdata report_nkro_t *report)
{
#ifdef RF_ENABLED
    if (KB_RF_ACTIVE()) {
        rf_send_nkro(report);
        return;
    }
#endif
    usb_send_nkro(report);
}

void kb_send_extra(__xdata report_extra_t *report)
{
#ifdef RF_ENABLED
    if (KB_RF_ACTIVE()) {
        rf_send_extra(report);
        return;
    }
#endif
    usb_send_extra(report);
}

#if DEBUG == 1
extern volatile bool sleep_log_pending;
extern uint8_t       sleep_log_int4;
extern uint8_t       sleep_log_usbif1;
extern uint8_t       sleep_log_mode;
extern uint16_t      sleep_log_count;
#endif

void kb_update()
{
#if DEBUG == 1
    if (sleep_log_pending && usb_console_ready()) {
        sleep_log_pending = false;
        dprintf("WAKE #%u mode=%u int4=%02x usbif1=%02x\r\n", sleep_log_count, sleep_log_mode, sleep_log_int4, sleep_log_usbif1);
    }
#endif

#ifdef RF_ENABLED
    if (!KB_RF_ACTIVE()) {
        return;
    }

    if (link_pairing_armed && link_hold_keycode) {
        if (link_hold_ticks < LINK_PAIRING_HOLD_TICKS) {
            link_hold_ticks++;
        } else {
            const rf_mode_t link = (link_hold_keycode == LNK_24G) ? RF_MODE_2_4G : (rf_mode_t)(RF_MODE_BT1 + (link_hold_keycode - LNK_BT1));
            dprintf("rf link pairing %02x\r\n", link);
            keyboard_state.paired    = 0;
            keyboard_state.connected = 0;
            rf_set_link_pairing(link, &keyboard_state);
            link_pairing_armed = false;
        }
    }

    rf_link_supervisor(&keyboard_state);
    rf_send_pending_flush();
    rf_blanking_tick();
#endif
}
