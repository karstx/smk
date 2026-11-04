#include "kbdef.h"
#include "layout.h"
#include "user_layout.h"
#include "report.h"
#include <stdint.h>

// Electrical order from the 6x21 schematic after removing numpad C18-C21.
// clang-format off
#define LAYOUT_TKL87( \
    K00_0,        K02_0, K03_0, K04_0, K05_0, K06_0, K07_0, K08_0, K09_0, K10_0, K11_0, K12_0, K13_0, K14_0, K15_0, K16_0, \
    K00_1, K01_1, K02_1, K03_1, K04_1, K05_1, K06_1, K07_1, K08_1, K09_1, K10_1, K11_1, K12_1, K13_1, K14_1, K15_1, K16_1, \
    K00_2, K01_2, K02_2, K03_2, K04_2, K05_2, K06_2, K07_2, K08_2, K09_2, K10_2, K11_2, K12_2, K13_2, K14_2, K15_2, K16_2, \
    K00_3, K01_3, K02_3, K03_3, K04_3, K05_3, K06_3, K07_3, K08_3, K09_3, K10_3, K11_3,        K13_3,                \
    K00_4, K01_4, K02_4, K03_4, K04_4, K05_4, K06_4, K07_4, K08_4, K09_4, K10_4,               K13_4,        K15_4,        \
    K00_5, K01_5, K02_5,               K05_5,               K08_5, K09_5, K10_5,               K13_5, K14_5, K15_5, K16_5  \
) { \
    { K00_0, KC_NO, K02_0, K03_0, K04_0, K05_0, K06_0, K07_0, K08_0, K09_0, K10_0, K11_0, K12_0, K13_0, K14_0, K15_0, K16_0 }, \
    { K00_1, K01_1, K02_1, K03_1, K04_1, K05_1, K06_1, K07_1, K08_1, K09_1, K10_1, K11_1, K12_1, K13_1, K14_1, K15_1, K16_1 }, \
    { K00_2, K01_2, K02_2, K03_2, K04_2, K05_2, K06_2, K07_2, K08_2, K09_2, K10_2, K11_2, K12_2, K13_2, K14_2, K15_2, K16_2 }, \
    { K00_3, K01_3, K02_3, K03_3, K04_3, K05_3, K06_3, K07_3, K08_3, K09_3, K10_3, K11_3, KC_NO, K13_3, KC_NO, KC_NO, KC_NO }, \
    { K00_4, K01_4, K02_4, K03_4, K04_4, K05_4, K06_4, K07_4, K08_4, K09_4, K10_4, KC_NO, KC_NO, K13_4, KC_NO, K15_4, KC_NO }, \
    { K00_5, K01_5, K02_5, KC_NO, KC_NO, K05_5, KC_NO, KC_NO, K08_5, K09_5, K10_5, KC_NO, KC_NO, K13_5, K14_5, K15_5, K16_5 }  \
}

#define _BL 0
#define _FL 1

const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_BL] = LAYOUT_TKL87(
        KC_ESC,     KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,  KC_PSCR, KC_SCRL, KC_PAUS,
        KC_GRV,     KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC, KC_INS,  KC_HOME, KC_PGUP,
        KC_TAB,     KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS, KC_DEL,  KC_END,  KC_PGDN,
        KC_CAPS,    KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,          KC_ENT,
        KC_LSFT,    KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH,          KC_RSFT,          KC_UP,
        KC_LCTL,    KC_LGUI, KC_LALT,                   KC_SPC,                    KC_RALT, MO(_FL),  KC_APP,           KC_RCTL, KC_LEFT, KC_DOWN, KC_RGHT
    ),
    [_FL] = LAYOUT_TKL87(
        BLE_CLEAR,  _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______,    BLE1_CTL,BLE2_CTL,_______, RF24_CTL, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______,    _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______,    _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,          _______,
        _______,    _______, _______, _______, _______, _______, _______, _______, SPD_DN,  SPD_UP,           _______,          _______, BRI_UP,
        _______,    GUI_LOCK, _______,                  _______,                   _______, _______, _______,          _______, FX_NEXT, BRI_DN,  _______
    )
};
// clang-format on
