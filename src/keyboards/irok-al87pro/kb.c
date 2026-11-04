#include "report.h"
#include "usb.h"
#include "kbdef.h"
#include "byk916_bk3632_link.h"
#include <stdbool.h>

#ifdef PER_KEY_RGB_ENABLE
#include "per_key_rgb.h"

__code const uint8_t matrix_to_led[MATRIX_ROWS][MATRIX_COLS] = {
    {0, PER_KEY_RGB_NONE, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32},
    {33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49},
    {50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, PER_KEY_RGB_NONE, 62, PER_KEY_RGB_NONE, PER_KEY_RGB_NONE, PER_KEY_RGB_NONE},
    {63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, PER_KEY_RGB_NONE, PER_KEY_RGB_NONE, 74, PER_KEY_RGB_NONE, 75, PER_KEY_RGB_NONE},
    {76, 77, 78, PER_KEY_RGB_NONE, PER_KEY_RGB_NONE, 79, PER_KEY_RGB_NONE, PER_KEY_RGB_NONE, 80, 81, 82, PER_KEY_RGB_NONE, PER_KEY_RGB_NONE, 83, 84, 85, 86},
};
#endif

extern void indicators_next_effect(void);
extern void indicators_brightness_up(void);
extern void indicators_brightness_down(void);
extern void indicators_speed_up(void);
extern void indicators_speed_down(void);

static bool gui_locked;

bool kb_gui_locked(void)
{
    return gui_locked;
}

void kb_init(void)
{
    gui_locked = false;
    byk916_bk3632_link_init();
}

bool kb_process_record(uint16_t keycode, bool pressed)
{
    if (gui_locked && (keycode == KC_LGUI || keycode == KC_RGUI)) {
        return false;
    }
    if (keycode == BLE1_CTL || keycode == BLE2_CTL || keycode == RF24_CTL) {
        byk916_bk3632_ble_key(keycode == RF24_CTL ? 4u : (keycode == BLE2_CTL ? 2u : 1u), pressed);
        return false;
    }
    if (keycode == BLE_CLEAR) {
        byk916_bk3632_ble_clear_key(pressed);
        return false;
    }
    if (!pressed) {
        return keycode < FX_NEXT || keycode > RF24_CTL;
    }

    switch (keycode) {
        case FX_NEXT:
            indicators_next_effect();
            return false;
        case BRI_UP:
            indicators_brightness_up();
            return false;
        case BRI_DN:
            indicators_brightness_down();
            return false;
        case SPD_UP:
            indicators_speed_up();
            return false;
        case SPD_DN:
            indicators_speed_down();
            return false;
        case GUI_LOCK:
            gui_locked = !gui_locked;
            return false;
        case BLE_CLEAR:
            return false;
        case USB_CTL:
            byk916_bk3632_usb_select();
            return false;
        default:
            return true;
    }
}

void kb_send_report(__xdata report_keyboard_t *report)
{
    if (byk916_bk3632_output_enabled() && byk916_bk3632_usb_selected()) usb_send_report(report);
    byk916_bk3632_send_6kro(report);
}

void kb_send_nkro(__xdata report_nkro_t *report)
{
    if (byk916_bk3632_output_enabled() && byk916_bk3632_usb_selected()) usb_send_nkro(report);
    byk916_bk3632_send_nkro(report);
}

void kb_send_extra(__xdata report_extra_t *report)
{
    if (byk916_bk3632_output_enabled() && byk916_bk3632_usb_selected()) usb_send_extra(report);
    byk916_bk3632_send_extra(report);
}
