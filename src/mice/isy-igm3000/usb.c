// USB device layer for the ISY IGM3000.
// EP0 state machine adapted from smk src/smk/usb.c; descriptors follow the stock
// mouse layout so hosts and unmodified sinowisp see the same interfaces:
//   iface 0: boot mouse, EP1 IN
//   iface 1: keyboard (report 1), consumer (2), ISP feature (5, sinowisp),
//            diag feature (7, GET), command feature (8, SET), EP2 IN
#include "usb.h"
#include "board.h"
#include "usbhw.h"
#include "isp.h"
#include <stdint.h>

#define EP0_PACKET 8u

// standard requests
#define REQ_GET_STATUS        0x00
#define REQ_CLEAR_FEATURE     0x01
#define REQ_SET_FEATURE       0x03
#define REQ_SET_ADDRESS       0x05
#define REQ_GET_DESCRIPTOR    0x06
#define REQ_GET_CONFIGURATION 0x08
#define REQ_SET_CONFIGURATION 0x09
#define REQ_GET_INTERFACE     0x0a
#define REQ_SET_INTERFACE     0x0b
// HID class requests
#define HID_GET_REPORT   0x01
#define HID_GET_IDLE     0x02
#define HID_GET_PROTOCOL 0x03
#define HID_SET_REPORT   0x09
#define HID_SET_IDLE     0x0a
#define HID_SET_PROTOCOL 0x0b

#define REPORT_TYPE_INPUT   1
#define REPORT_TYPE_OUTPUT  2
#define REPORT_TYPE_FEATURE 3

#define REPORT_ID_KEYBOARD 1
#define REPORT_ID_CONSUMER 2
#define REPORT_ID_ISP      5
#define REPORT_ID_DIAG     7
#define REPORT_ID_CMD      8
#define ISP_CMD_ENTER      0x75

// ---------------------------------------------------------------- descriptors

// iface 0: identical to the stock mouse report (5 buttons, 16-bit X/Y, wheel, AC pan)
static const __code uint8_t hid_report_mouse[] = {
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x05, 0x81, 0x02, 0x95, 0x03, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x00, 0x80, 0x26, 0xff, 0x7f, 0x75, 0x10, 0x95, 0x02, 0x81, 0x06, 0x09, 0x38, 0x15, 0x80, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0x05, 0x0c, 0x0a, 0x38, 0x02, 0x95, 0x01, 0x81, 0x06, 0xc0, 0xc0,
};

static const __code uint8_t hid_report_extra[] = {
    // keyboard, report 1: modifiers + 6 keys
    0x05,
    0x01,
    0x09,
    0x06,
    0xa1,
    0x01,
    0x85,
    REPORT_ID_KEYBOARD,
    0x05,
    0x07,
    0x19,
    0xe0,
    0x29,
    0xe7,
    0x15,
    0x00,
    0x25,
    0x01,
    0x75,
    0x01,
    0x95,
    0x08,
    0x81,
    0x02,
    0x95,
    0x06,
    0x75,
    0x08,
    0x15,
    0x00,
    0x26,
    0xff,
    0x00,
    0x05,
    0x07,
    0x19,
    0x00,
    0x2a,
    0xff,
    0x00,
    0x81,
    0x00,
    0xc0,
    // consumer, report 2: one 16-bit usage
    0x05,
    0x0c,
    0x09,
    0x01,
    0xa1,
    0x01,
    0x85,
    REPORT_ID_CONSUMER,
    0x15,
    0x00,
    0x26,
    0xff,
    0x03,
    0x19,
    0x00,
    0x2a,
    0xff,
    0x03,
    0x75,
    0x10,
    0x95,
    0x01,
    0x81,
    0x00,
    0xc0,
    // vendor: ISP feature report 5 (5 bytes) -- same shape as stock, used by sinowisp
    0x06,
    0x00,
    0xff,
    0x09,
    0x01,
    0xa1,
    0x01,
    0x85,
    REPORT_ID_ISP,
    0x15,
    0x00,
    0x26,
    0xff,
    0x00,
    0x09,
    0x00,
    0x75,
    0x08,
    0x95,
    0x05,
    0xb1,
    0x02,
    0xc0,
    // vendor: diag feature report 7 (15 bytes, GET)
    0x06,
    0x00,
    0xff,
    0x09,
    0x02,
    0xa1,
    0x01,
    0x85,
    REPORT_ID_DIAG,
    0x15,
    0x00,
    0x26,
    0xff,
    0x00,
    0x09,
    0x00,
    0x75,
    0x08,
    0x95,
    DIAG_LEN,
    0xb1,
    0x02,
    0xc0,
    // vendor: command feature report 8 (7 bytes, SET)
    0x06,
    0x00,
    0xff,
    0x09,
    0x03,
    0xa1,
    0x01,
    0x85,
    REPORT_ID_CMD,
    0x15,
    0x00,
    0x26,
    0xff,
    0x00,
    0x09,
    0x00,
    0x75,
    0x08,
    0x95,
    0x07,
    0xb1,
    0x02,
    0xc0,
};

static const __code uint8_t desc_device[18] = {
    18, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, EP0_PACKET, (uint8_t)USB_VID, (uint8_t)(USB_VID >> 8), (uint8_t)USB_PID, (uint8_t)(USB_PID >> 8), 0x00, 0x01, // bcdDevice 1.00
    1,  2,    3,                                                                                                                                          // strings
    1,
};

#define CONFIG_TOTAL (9 + 9 + 9 + 7 + 9 + 9 + 7)
static const __code uint8_t desc_config[CONFIG_TOTAL] = {
    9,
    0x02,
    CONFIG_TOTAL,
    0,
    2,
    1,
    0,
    0x80,
    0x80, // bus powered, 256 mA (stock value)
    // iface 0: HID boot mouse
    9,
    0x04,
    0,
    0,
    1,
    0x03,
    0x01,
    0x02,
    0,
    9,
    0x21,
    0x11,
    0x01,
    0,
    1,
    0x22,
    sizeof(hid_report_mouse),
    0,
    7,
    0x05,
    0x81,
    0x03,
    8,
    0,
    1,
    // iface 1: HID (keyboard/consumer/vendor)
    9,
    0x04,
    1,
    0,
    1,
    0x03,
    0x00,
    0x00,
    0,
    9,
    0x21,
    0x11,
    0x01,
    0,
    1,
    0x22,
    sizeof(hid_report_extra),
    0,
    7,
    0x05,
    0x82,
    0x03,
    16,
    0,
    1,
};

#define HID_DESC0_OFFSET 18
#define HID_DESC1_OFFSET 43

static const __code uint8_t desc_langid[4] = {4, 0x03, 0x09, 0x04};

static const __code char *const __code usb_strings[3] = {
    USB_STR_MANUFACTURER,
    USB_STR_PRODUCT,
    USB_STR_SERIAL,
};

// ---------------------------------------------------------------- state

typedef enum {
    EP0_IDLE,
    EP0_DATA_IN,
    EP0_STATUS_OUT,
    EP0_DATA_OUT,
    EP0_STATUS_IN,
} ep0_stage_t;

typedef enum {
    OUT_NONE,
    OUT_ISP,
    OUT_CMD,
} ep0_out_target_t;

static __xdata uint8_t scratch[64];
__xdata uint8_t        diag[DIAG_LEN + 1];
__xdata uint8_t        cmd_buf[8];
volatile uint8_t       cmd_pending;

static uint8_t        device_state; // 0 default, 1 addressed, 2 configured
static uint8_t        usb_addr;
static uint8_t        configuration;
static uint8_t        protocol0 = 1;
static uint8_t        protocol1 = 1;
static uint8_t        idle_rate;
static __bit          ever_configured;
static __bit          suspended;
static __bit          isp_pending;
static volatile __bit isp_requested;
static __bit          address_pending;
static volatile __bit ep1_free;

static ep0_stage_t      ep0_stage;
static ep0_out_target_t ep0_out_target;
static uint8_t          ep0_out_len;
static const uint8_t   *ep0_in_src;
static uint16_t         ep0_in_left;
static __bit            ep0_in_fills_wlength;
static __bit            ep0_in_done;

static __xdata uint8_t ep1_report[8];

// ---------------------------------------------------------------- helpers

static void reset_state(void)
{
    device_state    = 0;
    usb_addr        = 0;
    configuration   = 0;
    protocol0       = 1;
    protocol1       = 1;
    idle_rate       = 0;
    suspended       = 0;
    isp_pending     = 0;
    address_pending = 0;
    ep0_stage       = EP0_IDLE;
    ep1_free        = 1;
}

static void ep0_stall(void)
{
    ep0_stage = EP0_IDLE;
    STALL_EP0();
    SET_EP0_OUT_RDY;
}

static void ep0_ack(void)
{
    SET_EP0_OUT_RDY;
    ep0_stage = EP0_STATUS_IN;
    SET_EP0_CNT(0);
    SET_EP0_IN_RDY;
}

static void ep0_send_next(void)
{
    if (ep0_in_done) {
        ep0_stage = EP0_STATUS_OUT;
        return;
    }
    uint8_t len = (ep0_in_left > EP0_PACKET) ? EP0_PACKET : (uint8_t)ep0_in_left;
    for (uint8_t i = 0; i < len; i++) {
        EP0_IN_BUF[i] = ep0_in_src[i];
    }
    SET_EP0_CNT(len);
    SET_EP0_IN_RDY;
    ep0_in_src += len;
    ep0_in_left -= len;
    ep0_in_done = (len < EP0_PACKET) || (ep0_in_left == 0 && ep0_in_fills_wlength);
}

static void ep0_send(const uint8_t *src, uint16_t len, uint16_t max_len)
{
    if (max_len == 0) {
        ep0_ack();
        return;
    }
    if (len > max_len) {
        len = max_len;
    }
    ep0_in_src           = src;
    ep0_in_left          = len;
    ep0_in_fills_wlength = (len == max_len);
    ep0_in_done          = 0;
    ep0_stage            = EP0_DATA_IN;
    SET_EP0_OUT_RDY;
    ep0_send_next();
}

static void ep0_receive(ep0_out_target_t target, uint16_t len)
{
    if (len == 0 || len > EP0_PACKET) {
        ep0_stall();
        return;
    }
    ep0_out_target = target;
    ep0_out_len    = (uint8_t)len;
    ep0_stage      = EP0_DATA_OUT;
    SET_EP0_OUT_RDY;
}

// ---------------------------------------------------------------- requests

static void get_descriptor(uint8_t type, uint8_t index, uint8_t iface, uint16_t wlen)
{
    switch (type) {
        case 0x01:
            ep0_send(desc_device, sizeof(desc_device), wlen);
            return;
        case 0x02:
            if (index == 0) {
                ep0_send(desc_config, sizeof(desc_config), wlen);
                return;
            }
            break;
        case 0x03:
            if (index == 0) {
                ep0_send(desc_langid, sizeof(desc_langid), wlen);
                return;
            }
            if (index <= 3) {
                const __code char *s = usb_strings[index - 1];
                uint8_t            n = 2;
                while (*s && n < sizeof(scratch) - 1) {
                    scratch[n++] = (uint8_t)*s++;
                    scratch[n++] = 0;
                }
                scratch[0] = n;
                scratch[1] = 0x03;
                ep0_send(scratch, n, wlen);
                return;
            }
            break;
        case 0x21: // HID
            if (iface == 0) {
                ep0_send(desc_config + HID_DESC0_OFFSET, 9, wlen);
                return;
            }
            if (iface == 1) {
                ep0_send(desc_config + HID_DESC1_OFFSET, 9, wlen);
                return;
            }
            break;
        case 0x22: // HID report
            if (iface == 0) {
                ep0_send(hid_report_mouse, sizeof(hid_report_mouse), wlen);
                return;
            }
            if (iface == 1) {
                ep0_send(hid_report_extra, sizeof(hid_report_extra), wlen);
                return;
            }
            break;
        default:
            break;
    }
    ep0_stall();
}

static void standard_request(uint8_t rt, uint8_t req, uint16_t wvalue, uint16_t windex, uint16_t wlen)
{
    uint8_t in    = rt & 0x80;
    uint8_t recip = rt & 0x1f;

    switch (req) {
        case REQ_GET_STATUS:
            if (in) {
                scratch[0] = 0;
                scratch[1] = 0;
                if (recip == 2) { // endpoint halt status
                    if ((windex & 0xff) == 0x81) {
                        scratch[0] = (EP1CON & _IEP1STL) ? 1 : 0;
                    } else if ((windex & 0xff) == 0x82) {
                        scratch[0] = (EP2CON & _IEP2STL) ? 1 : 0;
                    }
                }
                ep0_send(scratch, 2, wlen);
                return;
            }
            break;

        case REQ_CLEAR_FEATURE:
        case REQ_SET_FEATURE:
            if (!in && recip == 2 && wvalue == 0) { // ENDPOINT_HALT
                uint8_t ep  = windex & 0xff;
                uint8_t set = (req == REQ_SET_FEATURE);
                if (ep == 0x81) {
                    if (set)
                        SET_EP1_IN_STALL;
                    else
                        EP1CON &= ~(_IEP1STL | _IEP1DTG);
                } else if (ep == 0x82) {
                    if (set)
                        SET_EP2_IN_STALL;
                    else
                        EP2CON &= ~(_IEP2STL | _IEP2DTG);
                }
                ep0_ack();
                return;
            }
            if (!in && recip == 0) { // device remote wakeup: not supported, accept
                ep0_ack();
                return;
            }
            break;

        case REQ_SET_ADDRESS:
            if (!in && wvalue <= 127) {
                usb_addr        = (uint8_t)wvalue;
                address_pending = 1;
                device_state    = usb_addr ? 1 : 0;
                ep0_ack();
                return;
            }
            break;

        case REQ_GET_DESCRIPTOR:
            if (in) {
                get_descriptor((uint8_t)(wvalue >> 8), (uint8_t)wvalue, (uint8_t)windex, wlen);
                return;
            }
            break;

        case REQ_GET_CONFIGURATION:
            if (in) {
                scratch[0] = configuration;
                ep0_send(scratch, 1, wlen);
                return;
            }
            break;

        case REQ_SET_CONFIGURATION:
            if (!in && usb_addr && wvalue <= 1) {
                configuration = (uint8_t)wvalue;
                device_state  = configuration ? 2 : 1;
                if (configuration) {
                    ever_configured = 1;
                }
                EP1CON &= ~(_IEP1STL | _IEP1DTG);
                EP2CON &= ~(_IEP2STL | _IEP2DTG);
                ep1_free = 1;
                ep0_ack();
                return;
            }
            break;

        case REQ_GET_INTERFACE:
            if (in && windex <= 1) {
                scratch[0] = 0;
                ep0_send(scratch, 1, wlen);
                return;
            }
            break;

        case REQ_SET_INTERFACE:
            if (!in && windex <= 1 && wvalue == 0) {
                ep0_ack();
                return;
            }
            break;

        default:
            break;
    }
    ep0_stall();
}

static void hid_request(uint8_t rt, uint8_t req, uint16_t wvalue, uint16_t windex, uint16_t wlen)
{
    uint8_t iface = (uint8_t)windex;
    uint8_t in    = rt & 0x80;
    uint8_t rtype = (uint8_t)(wvalue >> 8);
    uint8_t rid   = (uint8_t)wvalue;

    if ((rt & 0x1f) != 1 || iface > 1) {
        ep0_stall();
        return;
    }

    switch (req) {
        case HID_SET_REPORT:
            if (!in && iface == 1 && rtype == REPORT_TYPE_FEATURE) {
                if (rid == REPORT_ID_ISP) {
                    ep0_receive(OUT_ISP, wlen);
                    return;
                }
                if (rid == REPORT_ID_CMD) {
                    ep0_receive(OUT_CMD, wlen);
                    return;
                }
            }
            if (!in && iface == 1 && rtype == REPORT_TYPE_OUTPUT && rid == REPORT_ID_KEYBOARD) {
                ep0_receive(OUT_NONE, wlen); // keyboard LEDs: ignored
                return;
            }
            break;

        case HID_GET_REPORT:
            if (in && iface == 1 && rtype == REPORT_TYPE_FEATURE && rid == REPORT_ID_DIAG) {
                diag[0] = REPORT_ID_DIAG;
                ep0_send(diag, sizeof(diag), wlen);
                return;
            }
            if (in && iface == 1 && rtype == REPORT_TYPE_FEATURE && rid == REPORT_ID_ISP) {
                scratch[0] = REPORT_ID_ISP;
                for (uint8_t i = 1; i < 6; i++)
                    scratch[i] = 0;
                ep0_send(scratch, 6, wlen);
                return;
            }
            if (in && iface == 0 && rtype == REPORT_TYPE_INPUT) {
                for (uint8_t i = 0; i < 7; i++)
                    scratch[i] = 0;
                ep0_send(scratch, protocol0 ? 7 : 3, wlen);
                return;
            }
            break;

        case HID_SET_IDLE:
            if (!in) {
                idle_rate = (uint8_t)(wvalue >> 8);
                ep0_ack();
                return;
            }
            break;

        case HID_GET_IDLE:
            if (in) {
                scratch[0] = idle_rate;
                ep0_send(scratch, 1, wlen);
                return;
            }
            break;

        case HID_SET_PROTOCOL:
            if (!in && wvalue <= 1) {
                if (iface == 0)
                    protocol0 = (uint8_t)wvalue;
                else
                    protocol1 = (uint8_t)wvalue;
                ep0_ack();
                return;
            }
            break;

        case HID_GET_PROTOCOL:
            if (in) {
                scratch[0] = iface == 0 ? protocol0 : protocol1;
                ep0_send(scratch, 1, wlen);
                return;
            }
            break;

        default:
            break;
    }
    ep0_stall();
}

// ---------------------------------------------------------------- interrupt side

static void ep0_in_irq(void)
{
    if (ep0_stage == EP0_DATA_IN) {
        ep0_send_next();
    } else if (ep0_stage == EP0_STATUS_IN) {
        ep0_stage = EP0_IDLE;
        if (address_pending) {
            address_pending = 0;
            USBADDR         = usb_addr;
        }
        if (isp_pending) {
            isp_pending   = 0;
            isp_requested = 1; // jump happens in main (bootloader never RETIs)
        }
    }
}

static void setup_irq(void)
{
    if ((USBIF2 & _IEP0IF) && ep0_stage == EP0_STATUS_IN) {
        ep0_in_irq();
    }
    USBIF2 &= ~(_IEP0IF | _OEP0IF);
    ep0_stage       = EP0_IDLE;
    address_pending = 0;
    isp_pending     = 0;

    uint8_t  rt     = EP0_OUT_BUF[0];
    uint8_t  req    = EP0_OUT_BUF[1];
    uint16_t wvalue = EP0_OUT_BUF[2] | ((uint16_t)EP0_OUT_BUF[3] << 8);
    uint16_t windex = EP0_OUT_BUF[4] | ((uint16_t)EP0_OUT_BUF[5] << 8);
    uint16_t wlen   = EP0_OUT_BUF[6] | ((uint16_t)EP0_OUT_BUF[7] << 8);

    switch (rt & 0x60) {
        case 0x00:
            standard_request(rt, req, wvalue, windex, wlen);
            break;
        case 0x20:
            hid_request(rt, req, wvalue, windex, wlen);
            break;
        default:
            ep0_stall();
            break;
    }
}

static void ep0_out_irq(void)
{
    SET_EP0_OUT_RDY;
    if (ep0_stage == EP0_DATA_OUT) {
        if (GET_EP0_OUT_CNT() != ep0_out_len) {
            ep0_stall();
            return;
        }
        if (ep0_out_target == OUT_ISP) {
            if (EP0_OUT_BUF[0] == REPORT_ID_ISP && EP0_OUT_BUF[1] == ISP_CMD_ENTER) {
                isp_pending = 1;
            }
        } else if (ep0_out_target == OUT_CMD) {
            if (!cmd_pending && EP0_OUT_BUF[0] == REPORT_ID_CMD) {
                for (uint8_t i = 0; i < 8; i++) {
                    cmd_buf[i] = i < ep0_out_len ? EP0_OUT_BUF[i] : 0;
                }
                cmd_pending = 1;
            }
        }
        ep0_ack();
    } else if (ep0_stage == EP0_DATA_IN || ep0_stage == EP0_STATUS_OUT) {
        ep0_stage = EP0_IDLE;
    }
}

void usb_irq_dispatch(void)
{
    uint8_t if1 = USBIF1;

    if (if1 & (_USBRSTIF | _PUPIF)) {
        USBIF1 &= ~(_USBRSTIF | _PUPIF);
        USBCON |= _SWRST;
        NOP();
        NOP();
        NOP();
        NOP();
        NOP();
        NOP();
        USBCON &= ~_SWRST;
        reset_state();
        usb_hw_init();
        SET_EP0_OUT_RDY;
        return;
    }
    if (if1 & _SOFIF) {
        USBIF1 &= ~_SOFIF;
        suspended = 0;
    }
    if (if1 & _RESMIF) {
        USBIF1 &= ~_RESMIF;
        suspended = 0;
    }
    if (if1 & _SUSPIF) {
        USBIF1 &= ~_SUSPIF;
        if (device_state == 2) {
            suspended = 1;
        }
    }
    if (if1 & _SETUPIF) {
        USBIF1 &= ~_SETUPIF;
        USBIF1 &= ~(_OVERIF | _OW);
        setup_irq();
    }

    uint8_t if2 = USBIF2;
    if (if2 & _OEP0IF) {
        USBIF2 &= ~_OEP0IF;
        ep0_out_irq();
    }
    if (if2 & _IEP0IF) {
        USBIF2 &= ~_IEP0IF;
        ep0_in_irq();
    }
    if (if2 & _IEP2IF) {
        USBIF2 &= ~_IEP2IF;
        usb_hw_ep2_in_complete();
    }
    if (if2 & _IEP1IF) {
        USBIF2 &= ~_IEP1IF;
        usb_hw_ep1_in_complete();
        ep1_free = 1;
    }
}

// ---------------------------------------------------------------- main side

void usb_init(void)
{
    reset_state();
    ever_configured = 0;
    isp_requested   = 0;
    cmd_pending     = 0;
    usb_hw_init();
}

void usb_task(void)
{
    if (isp_requested) {
        isp_jump(); // clr EA; B=0xA5; A=0x5A; ljmp 0x7F00 (same as stock)
    }
}

uint8_t usb_is_configured(void)
{
    return device_state == 2;
}

uint8_t usb_ever_configured(void)
{
    return ever_configured;
}

uint8_t usb_is_suspended(void)
{
    return suspended;
}

uint8_t usb_boot_protocol(void)
{
    return protocol0 == 0;
}

static int8_t clamp8(int16_t v)
{
    return v > 127 ? 127 : (v < -127 ? -127 : (int8_t)v);
}

uint8_t usb_ep1_free(void)
{
    return ep1_free;
}

void usb_send_mouse(uint8_t buttons, int16_t x, int16_t y, int8_t wheel)
{
    if (device_state != 2) {
        return;
    }
    ep1_free      = 0; // set again by the IEP1 completion interrupt
    ep1_report[0] = buttons & 0x1f;
    if (protocol0 == 0) {
        ep1_report[1] = (uint8_t)clamp8(x);
        ep1_report[2] = (uint8_t)clamp8(y);
        usb_hw_ep1_in_send(ep1_report, 3);
        return;
    }
    ep1_report[1] = (uint8_t)x;
    ep1_report[2] = (uint8_t)((uint16_t)x >> 8);
    ep1_report[3] = (uint8_t)y;
    ep1_report[4] = (uint8_t)((uint16_t)y >> 8);
    ep1_report[5] = (uint8_t)wheel;
    ep1_report[6] = 0;
    usb_hw_ep1_in_send(ep1_report, 7);
}
