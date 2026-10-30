#include "usbhw.h"
#include "usb.h"
#include "interrupts.h"
#include "watchdog.h"
#include "delay.h"
#if DEBUG == 1
#    include "report.h"
#endif
#include <stdint.h>

// IEPxRDY does not read back the endpoint's busy state here, so completion is tracked in software instead of by polling.
static __bit ep1_in_busy;
static __bit ep2_in_busy;

#define EP_IN_DRAIN_TRIES 40

static void set_ep1_in_buffer(uint8_t *src, uint8_t len)
{
    if (len > EP1_BUF_SIZE) {
        return;
    }
    for (uint8_t i = 0; i < len; i++) {
        EP1_IN_BUF[i] = src[i];
    }
}

static void set_ep2_in_buffer(uint8_t *src, uint8_t len)
{
    if (len > EP2_BUF_SIZE) {
        return;
    }
    for (uint8_t i = 0; i < len; i++) {
        EP2_IN_BUF[i] = src[i];
    }
}

static void ep1_in_drain(void)
{
    uint8_t tries = 0;
    while (tries < EP_IN_DRAIN_TRIES && ep1_in_busy) {
        watchdog_kick();
        delay_us(40);
        tries++;
    }
    ep1_in_busy = 1;
}

static void ep2_in_drain(void)
{
    uint8_t tries = 0;
    while (tries < EP_IN_DRAIN_TRIES && ep2_in_busy) {
        watchdog_kick();
        delay_us(40);
        tries++;
    }
    ep2_in_busy = 1;
}

void usb_hw_init(void)
{
    ep1_in_busy = 0;
    ep2_in_busy = 0;

    uint8_t saved_page = INSCON;
    sfr_page_1();

    USBADDR = 0;
    EP1CON  = 0;
    EP2CON  = 0;
    USBIE1  = (_SETUPIE | _SOFIE | _RESMIE | _SUSPIE | _PBRSTIE);
    USBIE2  = (_OEP0IE | _IEP0IE | _IEP1IE | _IEP2IE);
    USBCON  = (_ENUSB | _SW1CON);

    INSCON = saved_page;
    IEN1 |= _EUSB_ETWI;
}

void usb_hw_deinit(void)
{
    uint8_t saved_page = INSCON;
    sfr_page_1();

    USBADDR = 0;
    USBCON &= ~(_ENUSB | _SW1CON | _SW2CON); // drop the module-enable bits

    INSCON = saved_page;
    IEN1 &= ~_EUSB_ETWI; // disable the USB interrupt
}

void usb_hw_ep1_in_send(uint8_t *src, uint8_t len)
{
    ep1_in_drain();

    uint8_t saved_page = INSCON;
    sfr_page_1();

    set_ep1_in_buffer(src, len);
    SET_EP1_CNT(len);
    SET_EP1_IN_RDY;

    INSCON = saved_page;
}

void usb_hw_ep2_in_send(uint8_t *src, uint8_t len)
{
    ep2_in_drain();

    uint8_t saved_page = INSCON;
    sfr_page_1();

    set_ep2_in_buffer(src, len);
    SET_EP2_CNT(len);
    SET_EP2_IN_RDY;

    INSCON = saved_page;
}

void usb_hw_ep1_in_complete(void)
{
    ep1_in_busy = 0;
}

void usb_hw_ep2_in_complete(void)
{
    ep2_in_busy = 0;
}

#if DEBUG == 1
bool usb_hw_ep2_in_free(void)
{
    return !ep2_in_busy;
}

void usb_hw_console_send(const __xdata uint8_t *data, uint8_t len)
{
    // console_task gates on usb_hw_ep2_in_free(), so leaving this clear lets the next line
    // overwrite the buffer before the host has collected this one.
    ep2_in_busy = 1;

    uint8_t saved_page = INSCON;
    sfr_page_1();

    EP2_IN_BUF[0] = REPORT_ID_CONSOLE;
    for (uint8_t i = 0; i < CONSOLE_REPORT_SIZE; i++) {
        EP2_IN_BUF[1 + i] = (i < len) ? data[i] : 0;
    }

    SET_EP2_CNT(1 + CONSOLE_REPORT_SIZE);
    SET_EP2_IN_RDY;

    INSCON = saved_page;
}
#endif

void usb_interrupt_handler(void) __interrupt(_INT_USB_TWI)
{
    // the USB block is on SFR page 1 and everything else on page 0, so borrow page 1 only for the handler.
    uint8_t saved_page = INSCON;
    sfr_page_1();

    usb_irq_dispatch();

    INSCON = saved_page;
}
