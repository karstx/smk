#pragma once

#include "sh68f90.h"
#include "keycodes.h"
#include <stdint.h>

#define MATRIX_ROWS 6
#define MATRIX_COLS 17

#define PER_KEY_RGB_COUNT 87u
#define PER_KEY_RGB_NONE  0xFFu

// Rows are active-low inputs. P5.3/P5.4 are packed into matrix bits 4/5.
#define KB_R0_P7_0 _P7_0
#define KB_R1_P7_1 _P7_1
#define KB_R2_P7_2 _P7_2
#define KB_R3_P7_3 _P7_3
#define KB_R4_P5_3 _P5_3
#define KB_R5_P5_4 _P5_4

#define KB_R_P7_MASK (uint8_t)(KB_R0_P7_0 | KB_R1_P7_1 | KB_R2_P7_2 | KB_R3_P7_3)
#define KB_R_P5_MASK (uint8_t)(KB_R4_P5_3 | KB_R5_P5_4)

// Columns are driven low one at a time and idle high.
#define KB_C0_P6_0  _P6_0
#define KB_C1_P6_1  _P6_1
#define KB_C2_P6_2  _P6_2
#define KB_C3_P6_3  _P6_3
#define KB_C4_P6_4  _P6_4
#define KB_C5_P6_5  _P6_5
#define KB_C6_P6_6  _P6_6
#define KB_C7_P6_7  _P6_7
#define KB_C8_P5_0  _P5_0
#define KB_C9_P5_1  _P5_1
#define KB_C10_P5_2 _P5_2
#define KB_C11_P5_7 _P5_7
#define KB_C12_P4_0 _P4_0
#define KB_C13_P4_2 _P4_2
#define KB_C14_P4_3 _P4_3
#define KB_C15_P4_5 _P4_5
#define KB_C16_P4_6 _P4_6

#define KB_C_P6_MASK 0xFFu
#define KB_C_P5_MASK (uint8_t)(KB_C8_P5_0 | KB_C9_P5_1 | KB_C10_P5_2 | KB_C11_P5_7)
#define KB_C_P4_MASK (uint8_t)(KB_C12_P4_0 | KB_C13_P4_2 | KB_C14_P4_3 | KB_C15_P4_5 | KB_C16_P4_6)

#define KB_C0  P6_0
#define KB_C1  P6_1
#define KB_C2  P6_2
#define KB_C3  P6_3
#define KB_C4  P6_4
#define KB_C5  P6_5
#define KB_C6  P6_6
#define KB_C7  P6_7
#define KB_C8  P5_0
#define KB_C9  P5_1
#define KB_C10 P5_2
#define KB_C11 P5_7
#define KB_C12 P4_0
#define KB_C13 P4_2
#define KB_C14 P4_3
#define KB_C15 P4_5
#define KB_C16 P4_6

// RGB VDR outputs. These pins map to all 18 SH68F90 PWM channels.
#define VDR0_R_P1_0 _P1_0
#define VDR0_G_P1_1 _P1_1
#define VDR0_B_P1_2 _P1_2
#define VDR1_R_P1_3 _P1_3
#define VDR1_G_P1_4 _P1_4
#define VDR1_B_P1_5 _P1_5
#define VDR2_R_P2_0 _P2_0
#define VDR2_G_P2_1 _P2_1
#define VDR2_B_P2_2 _P2_2
#define VDR3_R_P2_3 _P2_3
#define VDR3_G_P2_4 _P2_4
#define VDR3_B_P2_5 _P2_5
#define VDR4_R_P3_3 _P3_3
#define VDR4_G_P3_4 _P3_4
#define VDR4_B_P3_5 _P3_5
#define VDR5_R_P3_0 _P3_0
#define VDR5_G_P3_1 _P3_1
#define VDR5_B_P3_2 _P3_2

#define VDR_P1_MASK 0x3Fu
#define VDR_P2_MASK 0x3Fu
#define VDR_P3_MASK 0x3Fu

#define LED_PWM_R0 PWM20
#define LED_PWM_G0 PWM21
#define LED_PWM_B0 PWM22
#define LED_PWM_R1 PWM23
#define LED_PWM_G1 PWM24
#define LED_PWM_B1 PWM25
#define LED_PWM_R2 PWM10
#define LED_PWM_G2 PWM11
#define LED_PWM_B2 PWM12
#define LED_PWM_R3 PWM13
#define LED_PWM_G3 PWM14
#define LED_PWM_B3 PWM15
#define LED_PWM_R4 PWM03
#define LED_PWM_G4 PWM04
#define LED_PWM_B4 PWM05
#define LED_PWM_R5 PWM00
#define LED_PWM_G5 PWM01
#define LED_PWM_B5 PWM02

// C15 (electrical column, zero-based index 14) is an active-low PMOS high-side
// supply shared by its RGB column and three discrete indicators. VDR4 R/G/B
// are the NPN low-side controls for Caps/Win/Scroll.
#define LED_INDICATOR_COL 14u
#define LED_INDICATOR_CAPS (1u << 1)
#define LED_INDICATOR_SCROLL (1u << 2)

// 17 normal RGB column subframes plus isolated Caps/Win/Scroll subframes.
#define LED_INDICATOR_SUBFRAMES 3u
#define LED_SUBFRAMES_PER_SCAN (MATRIX_COLS + LED_INDICATOR_SUBFRAMES)

// BK3632 Bluetooth bridge wiring. BYK916 is the SPI master.
#define BK_BT_STATUS_P4_1 _P4_1
#define BK_BT_CSN_P7_5    _P7_5
#define BK_BT_MISO_P0_3   _P0_3
#define BK_BT_CLK_P4_7    _P4_7
#define BK_BT_MOSI_P0_4   _P0_4
#define BK_BT_MOT_P0_2    _P0_2

// Power board: P4.4 senses USB5V through a 20k/30k divider. P7.6 drives
// HFP6291 EN; its VCC output powers BK3632. P7.7 is ME4084 CHRG.
#define USB_POWER_SENSE_P4_4 _P4_4
#define DC_DC_ENABLE_P7_6    _P7_6
#define CHARGE_STATUS_P7_7   _P7_7

enum custom_keycodes {
    FX_NEXT = QK_KB,
    BRI_UP,
    BRI_DN,
    SPD_UP,
    SPD_DN,
    GUI_LOCK,
    BLE1_CTL,
    BLE_CLEAR,
    USB_CTL,
    BLE2_CTL,
    RF24_CTL,
};
