#pragma once
#include <stdint.h>

void usb_irq_dispatch(void); // called by sino51lib usbhw.c ISR (SFR page 1)

void    usb_init(void);
void    usb_task(void); // main-loop: performs the ISP jump when requested
uint8_t usb_is_configured(void);
uint8_t usb_ever_configured(void);
uint8_t usb_is_suspended(void);
uint8_t usb_boot_protocol(void); // 1 if host selected boot protocol on iface 0
uint8_t usb_ep1_free(void);      // host collected the previous mouse report

void usb_send_mouse(uint8_t buttons, int16_t x, int16_t y, int8_t wheel);

// Diag feature report 7 (GET) contents, filled by main.
#define DIAG_LEN 17
extern __xdata uint8_t diag[DIAG_LEN + 1];

// Command feature report 8 (SET): latched here, consumed by main.
extern volatile uint8_t cmd_pending;
extern __xdata uint8_t  cmd_buf[8];
