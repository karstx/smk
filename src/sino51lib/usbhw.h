#pragma once

#include "sfr.h"
#include <stdint.h>
#include <stdbool.h>

void usb_hw_init(void);
void usb_hw_deinit(void);

void usb_hw_ep1_in_send(uint8_t *src, uint8_t len);
void usb_hw_ep2_in_send(uint8_t *src, uint8_t len);

void usb_hw_ep1_in_complete(void);
void usb_hw_ep2_in_complete(void);

#if DEBUG == 1
bool usb_hw_ep2_in_free(void);
void usb_hw_console_send(const __xdata uint8_t *data, uint8_t len);
#endif

/**
 */
#define STALL_EP0()        \
    do {                   \
        SET_EP0_IN_STALL;  \
        SET_EP0_OUT_STALL; \
    } while (0)

#define SET_EP0_IN_RDY      \
    do {                    \
        EP0CON |= _IEP0RDY; \
    } while (0)
#define SET_EP0_IN_STALL    \
    do {                    \
        EP0CON |= _IEP0STL; \
    } while (0)
#define CANCEL_EP0_IN_STALL  \
    do {                     \
        EP0CON &= ~_IEP0STL; \
    } while (0)

#define SET_EP0_OUT_RDY     \
    do {                    \
        EP0CON |= _OEP0RDY; \
    } while (0)
#define SET_EP0_OUT_STALL   \
    do {                    \
        EP0CON |= _OEP0STL; \
    } while (0)
#define CANCEL_EP0_OUT_STALL \
    do {                     \
        EP0CON &= ~_OEP0STL; \
    } while (0)

#define SET_EP1_IN_RDY      \
    do {                    \
        EP1CON |= _IEP1RDY; \
    } while (0)
#define SET_EP1_IN_STALL    \
    do {                    \
        EP1CON |= _IEP1STL; \
    } while (0)
#define CANCEL_EP1_IN_STALL  \
    do {                     \
        EP1CON &= ~_IEP1STL; \
    } while (0)

#define SET_EP1_OUT_RDY     \
    do {                    \
        EP1CON |= _OEP1RDY; \
    } while (0)
#define SET_EP1_OUT_STALL   \
    do {                    \
        EP1CON |= _OEP1STL; \
    } while (0)
#define CANCEL_EP1_OUT_STALL \
    do {                     \
        EP1CON &= ~_OEP1STL; \
    } while (0)

#define SET_EP2_OUT_RDY     \
    do {                    \
        EP2CON |= _OEP2RDY; \
    } while (0)
#define SET_EP2_OUT_STALL   \
    do {                    \
        EP2CON |= _OEP2STL; \
    } while (0)
#define CANCEL_EP2_OUT_STALL \
    do {                     \
        EP2CON &= ~_OEP2STL; \
    } while (0)

#define SET_EP2_IN_RDY      \
    do {                    \
        EP2CON |= _IEP2RDY; \
    } while (0)
#define SET_EP2_IN_STALL    \
    do {                    \
        EP2CON |= _IEP2STL; \
    } while (0)
#define CANCEL_EP2_IN_STALL  \
    do {                     \
        EP2CON &= ~_IEP2STL; \
    } while (0)

#define CLEAR_EP0_CNT     \
    do {                  \
        IEP0CNT &= ~0x0f; \
    } while (0)
#define SET_EP0_CNT(COUNT) \
    do {                   \
        CLEAR_EP0_CNT;     \
        IEP0CNT |= COUNT;  \
    } while (0)
#define GET_EP0_OUT_CNT() (OEP0CNT & 0x0f)

#define CLEAR_EP1_CNT     \
    do {                  \
        IEP1CNT &= ~0x1f; \
    } while (0)
#define SET_EP1_CNT(COUNT) \
    do {                   \
        CLEAR_EP1_CNT;     \
        IEP1CNT |= COUNT;  \
    } while (0)

#define CLEAR_EP2_CNT     \
    do {                  \
        IEP2CNT &= ~0x7f; \
    } while (0)
#define SET_EP2_CNT(COUNT) \
    do {                   \
        CLEAR_EP2_CNT;     \
        IEP2CNT |= COUNT;  \
    } while (0)
