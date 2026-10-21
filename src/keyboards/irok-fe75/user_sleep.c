#include "kbdef.h"
#include "user_sleep.h"
#include "user_init.h"
#include "gpio.h"
#include "extint.h"
#include "power.h"
#include "usb.h"

#ifdef SLEEP_ENABLE

// Parking follows the stock firmware's sleep entry (0x9571): columns low so a
// key press pulls its row low and fires INT4 (rows stay pulled-up inputs), the
// unused and slider pins driven low with pull-ups off, and the wireless link
// pins released. Unlike the NuPhy Air60, P4.1 is the RF ACK line here and stock
// leaves it alone, so it isn't driven.
//
// Wake pins, measured on the board from EXF1: INT4 bit 0 = P7.0 (R0), 1 = P7.1
// (R1), 4 = P7.2 (R2), 5 = P7.3 (R3), 6 = P5.3 (R4), 7 = P5.4 (R5) - exactly the
// channels extint_wake_arm() enables (IENC 0xF3), so every key wakes the board.

#    if DEBUG == 1
// wake report, printed by kb_update once the debug console is back
volatile bool sleep_log_pending;
uint8_t       sleep_log_int4;
uint8_t       sleep_log_usbif1;
uint8_t       sleep_log_mode;
uint16_t      sleep_log_count;
#    endif

#    define SLEEP_P0_UNUSED   (uint8_t)(_P0_0 | _P0_1)
#    define SLEEP_P7_UNUSED   _P7_7
#    define SLEEP_MODE_SWITCH (uint8_t)(MODE_SW_A_P5_5 | MODE_SW_B_P5_6)

// Sleep only in the wireless positions, and only on battery. The slider pins
// aren't INT4 wake sources, so a sleeping board can't notice the slider moving
// back to USB; staying awake while a host has the USB link up (power comes from
// the cable then anyway) keeps the slider live. USB mode never sleeps.
user_sleep_mode_t user_sleep_supported(void)
{
#    ifdef RF_ENABLED
    if ((kb_mode_switch == MODE_SW_24G || kb_mode_switch == MODE_SW_BT) && !usb_is_configured()) {
        return USER_SLEEP_RF; // after the inactivity timeout
    }
#    endif
    return USER_SLEEP_NONE;
}

static void park_panel(void)
{
    // every column (LED common) low; with PWM parked the LEDs stay dark
    GPIO_OUTPUT(0, (uint8_t)(KB_C18_P0_5 | KB_C19_P0_6 | KB_C20_P0_7));
    GPIO_LOW(0, (uint8_t)(KB_C18_P0_5 | KB_C19_P0_6 | KB_C20_P0_7));
    GPIO_OUTPUT(4, (uint8_t)(KB_C12_P4_0 | KB_C13_P4_2 | KB_C14_P4_3 | KB_C15_P4_5 | KB_C16_P4_6));
    GPIO_LOW(4, (uint8_t)(KB_C12_P4_0 | KB_C13_P4_2 | KB_C14_P4_3 | KB_C15_P4_5 | KB_C16_P4_6));
    GPIO_OUTPUT(5, (uint8_t)(KB_C8_P5_0 | KB_C9_P5_1 | KB_C10_P5_2 | KB_C11_P5_7));
    GPIO_LOW(5, (uint8_t)(KB_C8_P5_0 | KB_C9_P5_1 | KB_C10_P5_2 | KB_C11_P5_7));
    GPIO_OUTPUT(6, 0xFF);
    GPIO_LOW(6, 0xFF);
    GPIO_OUTPUT(7, KB_C17_P7_4);
    GPIO_LOW(7, KB_C17_P7_4);

    GPIO_PULLUP_OFF(0, SLEEP_P0_UNUSED);
    GPIO_OUTPUT(0, SLEEP_P0_UNUSED);
    GPIO_LOW(0, SLEEP_P0_UNUSED);

    GPIO_PULLUP_OFF(5, SLEEP_MODE_SWITCH);
    GPIO_OUTPUT(5, SLEEP_MODE_SWITCH);
    GPIO_LOW(5, SLEEP_MODE_SWITCH);

    GPIO_PULLUP_OFF(7, SLEEP_P7_UNUSED);
    GPIO_OUTPUT(7, SLEEP_P7_UNUSED);
    GPIO_LOW(7, SLEEP_P7_UNUSED);

#    ifdef RF_ENABLED
    GPIO_INPUT(4, RF_BB_SPI_SCK_MASK);
    GPIO_INPUT(0, (uint8_t)(RF_BB_SPI_MOSI_MASK | RF_BB_SPI_MOT_MASK));
    GPIO_INPUT(7, RF_BB_SPI_CS_MASK);
#    endif
}

void user_sleep_prepare(void)
{
    park_panel();
    extint_wake_arm();
}

void user_sleep_wake(void)
{
    extint_wake_disable();

#    if DEBUG == 1
    sleep_log_int4   = power_wake_int4_flags;
    sleep_log_usbif1 = power_wake_usbif1;
    sleep_log_mode   = (uint8_t)kb_mode_switch;
    sleep_log_count++;
    sleep_log_pending = true;
#    endif

#    ifdef RF_ENABLED
    // nudge the wireless chip awake, as the NuPhy port does
    RF_BB_SPI_MOT = 0;
    GPIO_OUTPUT(0, RF_BB_SPI_MOT_MASK);
    RF_BB_SPI_MOT = 0;
#    endif

    // unparked pins go back to what user_init leaves them as; the slider pins
    // also need their latch high again to read as pulled-up inputs
    GPIO_INPUT(0, SLEEP_P0_UNUSED);
    GPIO_INPUT(7, SLEEP_P7_UNUSED);
    GPIO_HIGH(5, SLEEP_MODE_SWITCH);

    user_gpio_init();
}

#endif // SLEEP_ENABLE
