#include "power.h"
#include "interrupts.h"
#include "watchdog.h"
#include "delay.h"
#include "usb.h"
#include "clock.h"

#define SUSLO_POWERDOWN_KEY     0x55
#define USBIF1_BUS_EVENTS_CLEAR (uint8_t)(_SUSPIF | _SOFIF | _SETUPIF | _OW | _OVERIF)
#define USBIE1_RESUME_ARM       (uint8_t)(_PBRSTIE | _SUSPIE | _RESMIE | _SOFIE | _SETUPIE)
#define REGULATOR_SETTLE_US     500

#define USB_PAGE_ENTER()             \
    uint8_t usb_saved_page = INSCON; \
    sfr_page_1()
#define USB_PAGE_LEAVE() INSCON = usb_saved_page

static void usb_park(powerdown_mode_t mode)
{
    if (mode == POWERDOWN_KEEP_USB_ALIVE) {
        USB_PAGE_ENTER();
        USBCON |= _GOSUSP;
        USB_PAGE_LEAVE();
        return;
    }
    usb_deinit();
}

static void clock_tree_stop(void)
{
    CLKCON &= ~_FS;
    PLLCON &= ~_PLLFS;
    PLLCON &= ~_PLLON;
    CLKCON &= ~_OSC2ON;
}

static void wake_sources_arm(powerdown_mode_t mode)
{
    USB_PAGE_ENTER();
    USBIE1 |= (_PUPIE | _RESMIE | _PBRSTIE);
    USBIF1 &= USBIF1_BUS_EVENTS_CLEAR;
    USB_PAGE_LEAVE();

    if (mode == POWERDOWN_KEEP_USB_ALIVE) {
        IEN1 = _EUSB_ETWI;
    } else {
        IEN1 = 0;
        REGCON &= ~_REGEN;
    }
}

static void halt_until_wake(void)
{
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
    if (mode == POWERDOWN_KEEP_USB_ALIVE) {
        USB_PAGE_ENTER();
        USBIF1 &= ~_SUSPIF;
        USBCON &= ~_GOSUSP;
        USBIE1 = USBIE1_RESUME_ARM;
        USB_PAGE_LEAVE();

        IEN1 |= _EUSB_ETWI;
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

    halt_until_wake();

    clock_init();
    usb_resume(mode);
}
