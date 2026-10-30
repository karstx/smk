#pragma once

#include "sh68f90.h"
#include "keycodes.h"

// The stock firmware scans 21 columns because it is shared with a larger board;
// on the FE75 Pro only C0-C15 carry switches, so only those are scanned.
#define MATRIX_ROWS 6
#define MATRIX_COLS 16

// 4 LED subframes (400 us each) per key sweep: ~125 Hz LED refresh instead of
// ~60 Hz, which flickered. A sweep then comes every ~2 ms.
#define LED_SUBFRAMES_PER_SCAN 4

// per-key lockout after each reported change, ~2 ms per sweep: long enough to
// swallow contact chatter (which doubled presses), short enough for fast typing
#define MATRIX_DEBOUNCE_SCANS 3

// replay key changes in the order the scan saw them (Shift + letter typed fast
// came out lowercase when the main loop lagged a few sweeps)
#define MATRIX_EVENT_QUEUE 16

// LED colour slots within a row's three PWM channels (confirmed on the board with LED_TEST_PATTERN)
#define LED_SLOT_R 0
#define LED_SLOT_G 1
#define LED_SLOT_B 2

// White balance per channel (x/256): full R+G+B reads purple on this board
#define LED_GAIN_R 200
#define LED_GAIN_G 255
#define LED_GAIN_B 170

// RGB channel pins (PWM): P1.0-P1.5, P2.0-P2.5, P3.0-P3.5
#define LED_P1_MASK 0x3F
#define LED_P2_MASK 0x3F
#define LED_P3_MASK 0x3F

// 3-position mode slider, read like stock (0x9FBF); positions confirmed on the board
#define MODE_SW_A      P5_5
#define MODE_SW_B      P5_6
#define MODE_SW_A_P5_5 _P5_5
#define MODE_SW_B_P5_6 _P5_6

typedef enum {
    MODE_SW_USB = 0, // both high
    MODE_SW_24G,     // P5.5 low
    MODE_SW_BT,      // P5.6 low
    MODE_SW_UNKNOWN, // before the first debounced read
} mode_switch_t;

extern mode_switch_t kb_mode_switch;

// Wireless chip link: BK3632-compatible bit-banged SPI, decoded from stock
// (send 0xB383, receive 0xC493, framing 0x9D07). Same ports as the NuPhy Air60.
#define RF_BB_SPI_CS   P7_5
#define RF_BB_SPI_SCK  P4_7
#define RF_BB_SPI_MISO P0_3
#define RF_BB_SPI_MOSI P0_4
#define RF_BB_SPI_MOT  P0_2
#define RF_BB_SPI_ACK  P4_1

#define RF_BB_SPI_CS_MASK   _P7_5
#define RF_BB_SPI_SCK_MASK  _P4_7
#define RF_BB_SPI_MISO_MASK _P0_3
#define RF_BB_SPI_MOSI_MASK _P0_4
#define RF_BB_SPI_MOT_MASK  _P0_2
#define RF_BB_SPI_ACK_MASK  _P4_1

enum custom_keycodes {
    GUI_LCK = SAFE_RANGE, // Fn + F9: toggle the Windows/GUI key lock
    LED_TOG,              // Fn + F4: backlight on/off
    BRI_UP,               // Fn + F5: backlight brighter
    BRI_DN,               // Fn + F6: backlight dimmer
    SPD_DN,               // Fn + F7: animation slower
    SPD_UP,               // Fn + F8: animation faster
    FX_NEXT,              // Fn + F12: next backlight effect
    LNK_BT1,              // Fn + 1/2/3: Bluetooth host 1-3 (hold to pair)
    LNK_BT2,
    LNK_BT3,
    LNK_24G, // Fn + 4: 2.4 GHz (hold to pair the dongle)

    KB_SAFE_RANGE,
};

// Row Pin Bits
#define KB_R0_P7_0 _P7_0
#define KB_R1_P7_1 _P7_1
#define KB_R2_P7_2 _P7_2
#define KB_R3_P7_3 _P7_3
#define KB_R4_P5_3 _P5_3
#define KB_R5_P5_4 _P5_4

// Row Pins
#define KB_R0 P7_0
#define KB_R1 P7_1
#define KB_R2 P7_2
#define KB_R3 P7_3
#define KB_R4 P5_3
#define KB_R5 P5_4

// Column Pin Bits
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
#define KB_C17_P7_4 _P7_4
#define KB_C18_P0_5 _P0_5
#define KB_C19_P0_6 _P0_6
#define KB_C20_P0_7 _P0_7

// Column Pins
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
#define KB_C17 P7_4
#define KB_C18 P0_5
#define KB_C19 P0_6
#define KB_C20 P0_7
