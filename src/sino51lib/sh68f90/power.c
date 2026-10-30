#include "power.h"
#include "interrupts.h"
#include "watchdog.h"
#include "delay.h"
#include "usb.h"
#include "clock.h"
#include "extint.h"

#define SUSLO_POWERDOWN_KEY     0x55
#define USBIF1_BUS_EVENTS_CLEAR (uint8_t)(_SUSPIF | _SOFIF | _SETUPIF | _OW | _OVERIF)
#define USBIE1_RESUME_ARM       (uint8_t)(_PBRSTIE | _SUSPIE | _RESMIE | _SOFIA | _SETUPIE | _OVERIE)
#define REGULATOR_SETTLE_US     500

static volatile uint8_t int4_woke;

volatile uint8_t power_wake_int4_flags;
volatile uint8_t power_wake_usbif1;

static void usb_park(powerdown_mode_t mode)
{
    if (mode == POWERDOWN_KEEP_USB_ALIVE) {
        USBCON |= _GOSUSP;
        return;
    }
    usb_deinit();
}

static void clock_tree_stop(void)
{
    CLKCON &= ~_FS;
    PLLCON &= ~_PLLFS;
    PLLCON &= ~_PLLON;
    CLKCON &= ~_HFON;
}

static void wake_sources_arm(powerdown_mode_t mode)
{
    USBIE1 |= (_BOOTS | _RESMIE | _PBRSTIE);
    USBIF1 &= USBIF1_BUS_EVENTS_CLEAR;

    if (mode == POWERDOWN_KEEP_USB_ALIVE) {
        IEN1 = _EUSB;
    } else {
        IEN1 = 0;
        REGCON &= ~_REGEN;
    }
}

static void halt_until_wake(void)
{
    // The key write and PCON.PD must be consecutive instructions; nothing may
    // come between them.
    // clang-format off
    __asm
        nop
    __endasm;
    // clang-format on
    SUSLO = SUSLO_POWERDOWN_KEY;
    PCON |= _PD;
    // clang-format off
    __asm
        nop
        nop
        nop
        nop
        nop
        nop
    __endasm;
    // clang-format on
}

static void usb_resume(powerdown_mode_t mode)
{
    USBIF1 &= ~_SUSPIF;
    USBCON &= ~_GOSUSP;
    if (int4_woke) {
        USBCON |= _WKUP;
        int4_woke = 0;
    }

    if (mode == POWERDOWN_KEEP_USB_ALIVE) {
        USBIE1 = USBIE1_RESUME_ARM;
        IEN1 |= _EUSB;
        usb_suspended = 0;
        return;
    }

    REGCON |= _REGEN;
    delay_us(REGULATOR_SETTLE_US);
    usb_init();
}

void power_enter_powerdown(powerdown_mode_t mode)
{
    usb_park(mode);
    clock_tree_stop();
    wake_sources_arm(mode);

    watchdog_kick();
    int4_woke             = 0;
    power_wake_int4_flags = 0;

    halt_until_wake();
    power_wake_usbif1 = USBIF1;
    clock_wake_restart();
    usb_resume(mode);
}

void int4_interrupt_handler(void) __interrupt(_INT_INT4)
{
    power_wake_int4_flags = EXF1;
    extint_wake_clear();
    int4_woke = 1;
}
