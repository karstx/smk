#include "usb.h"
#include "isp.h"
#include "usbdef.h"
#include "usbhw.h"
#include "sfr.h"
#include "delay.h"
#include <stdint.h>

/*
 * The bootloader keeps the identity the SinoWealth ISP bootloaders have always
 * enumerated with, because that is what flashing tools look for. What it does
 * not keep is their descriptor set, which is what makes them enumerate badly:
 *
 *   - the HID interface declared bNumEndpoints = 0. HID 1.11 4.4 requires an
 *     interrupt IN endpoint, and a host that enforces it either refuses the
 *     interface or falls back to a generic driver. There is one here.
 *   - the two reports sat in two top-level application collections, which
 *     Windows splits into two HID device paths, so a tool had to find and open
 *     both halves. One collection carries both reports here.
 *   - Logical Maximum was written `25 FF`, and HID item data is signed, so it
 *     read as -1 against a Logical Minimum of 0. It is `26 FF 00` here.
 *   - nothing identified the device: no strings at all, and one VID:PID shared
 *     by every SinoWealth keyboard ever put into ISP mode. The strings here
 *     name the board and carry the identity its application firmware runs under.
 */
#define BOOT_USB_VID 0x0603u
#define BOOT_USB_PID 0x1020u

#ifdef sfr_page_1
#    define USB_PAGE_ENTER()         \
        uint8_t saved_page = INSCON; \
        sfr_page_1()
#    define USB_PAGE_LEAVE() INSCON = saved_page
#else
#    define USB_PAGE_ENTER()
#    define USB_PAGE_LEAVE()
#endif

#ifndef _SOFIA
#    define _SOFIA _SOFIE
#endif

#define EP1_PACKET_SIZE 8u
#define EP1_INTERVAL_MS 10u

enum usb_string_index {
    USB_STRING_LANGUAGE_ID   = 0,
    USB_STRING_MANUFACTURER  = 1,
    USB_STRING_PRODUCT       = 2,
    USB_STRING_SERIAL_NUMBER = 3,
};

typedef enum {
    EP0_IDLE,
    EP0_IN_DATA,
    EP0_OUT_DATA,
    EP0_STATUS_OUT,
    EP0_STATUS_IN,
} ep0_state_t;

typedef enum {
    EP0_SRC_CODE,
    EP0_SRC_SCRATCH,
    EP0_SRC_REPORT,
} ep0_source_t;

// clang-format off
static const __code uint8_t device_desc[] = {
    18,
    USB_DESC_DEVICE,
    0x00, 0x02,                         // bcdUSB 2.00
    USB_DEV_CLASS_PER_INTERFACE,
    USB_DEV_SUBCLASS_PER_INTERFACE,
    USB_DEV_PROTOCOL_PER_INTERFACE,
    EP0_BUF_SIZE,
    (uint8_t)BOOT_USB_VID, (uint8_t)(BOOT_USB_VID >> 8),
    (uint8_t)BOOT_USB_PID, (uint8_t)(BOOT_USB_PID >> 8),
    0x00, 0x01,                         // bcdDevice 1.00
    USB_STRING_MANUFACTURER,
    USB_STRING_PRODUCT,
    USB_STRING_SERIAL_NUMBER,
    1,
};

static const __code uint8_t hid_report_desc[] = {
    0x06, 0x00, 0xff,                   // Usage Page (Vendor 0xff00)
    0x09, 0x01,                         // Usage (1)
    0xa1, 0x01,                         // Collection (Application)
        0x85, ISP_REPORT_CMD,           //   Report ID (5)
        0x09, 0x02,                     //   Usage (2)
        0x15, 0x00,                     //   Logical Minimum (0)
        0x26, 0xff, 0x00,               //   Logical Maximum (255)
        0x75, 0x08,                     //   Report Size (8)
        0x95, ISP_CMD_LENGTH - 1u,      //   Report Count (5)
        0xb1, 0x02,                     //   Feature (Data,Var,Abs)

        0x85, ISP_REPORT_XFER,          //   Report ID (6)
        0x09, 0x03,                     //   Usage (3)
        0x15, 0x00,                     //   Logical Minimum (0)
        0x26, 0xff, 0x00,               //   Logical Maximum (255)
        0x75, 0x08,                     //   Report Size (8)
        0x96,                           //   Report Count (one page plus the opcode)
            (uint8_t)((ISP_XFER_LENGTH - 1u) & 0xffu),
            (uint8_t)((ISP_XFER_LENGTH - 1u) >> 8),
        0xb1, 0x02,                     //   Feature (Data,Var,Abs)

        // Nothing is sent on it. An HID interface owes the host an interrupt IN
        // endpoint, and an endpoint with no report to carry is what a descriptor
        // parser trips over.
        0x85, ISP_REPORT_STATUS,        //   Report ID (7)
        0x09, 0x04,                     //   Usage (4)
        0x15, 0x00,                     //   Logical Minimum (0)
        0x26, 0xff, 0x00,               //   Logical Maximum (255)
        0x75, 0x08,                     //   Report Size (8)
        0x95, EP1_PACKET_SIZE - 1u,     //   Report Count (7)
        0x81, 0x02,                     //   Input (Data,Var,Abs)
    0xc0,                               // End Collection
};

// the configuration descriptor carries the HID one inline; GET_DESCRIPTOR(HID) hands back that slice
#define HID_DESC_OFFSET (9u + 9u)
#define CONFIG_DESC_LEN (9u + 9u + 9u + 7u)

static const __code uint8_t config_desc[CONFIG_DESC_LEN] = {
    9,
    USB_DESC_CONFIGURATION,
    CONFIG_DESC_LEN, 0,
    1,                                  // bNumInterfaces
    1,                                  // bConfigurationValue
    0,                                  // iConfiguration
    USB_ATTR_RESERVED_1,
    50,                                 // bMaxPower: 100mA, nothing but the MCU is powered

    9,
    USB_DESC_INTERFACE,
    0,                                  // bInterfaceNumber
    0,                                  // bAlternateSetting
    1,                                  // bNumEndpoints
    USB_IFACE_CLASS_HID,
    USB_IFACE_SUBCLASS_NONE,
    USB_IFACE_PROTOCOL_BOOT,
    0,                                  // iInterface

    9,
    USB_DESC_CLASS_HID,
    0x11, 0x01,                         // bcdHID 1.11
    0,                                  // bCountryCode
    1,                                  // bNumDescriptors
    USB_DESC_CLASS_REPORT,
    (uint8_t)(sizeof(hid_report_desc) & 0xffu),
    (uint8_t)(sizeof(hid_report_desc) >> 8),

    7,
    USB_DESC_ENDPOINT,
    1 | USB_DIR_IN,
    USB_XFER_INTERRUPT,
    EP1_PACKET_SIZE, 0,
    EP1_INTERVAL_MS,
};

static const __code uint8_t langid_desc[] = {4, USB_DESC_STRING, 0x09, 0x04};
// clang-format on

#ifndef BOOT_USB_MANUFACTURER
#    define BOOT_USB_MANUFACTURER "sino51lib"
#endif
#ifndef BOOT_USB_PRODUCT
#    define BOOT_USB_PRODUCT "ISP Bootloader"
#endif
// the application's VID:PID is what tells one board in ISP mode from another, so builds should set it
#ifndef BOOT_USB_SERIAL
#    define BOOT_USB_SERIAL ""
#endif

static const __code char *const __code strings[] = {
    [USB_STRING_MANUFACTURER - 1]  = BOOT_USB_MANUFACTURER,
    [USB_STRING_PRODUCT - 1]       = BOOT_USB_PRODUCT,
    [USB_STRING_SERIAL_NUMBER - 1] = BOOT_USB_SERIAL,
};

#define SCRATCH_SIZE 96u

static __xdata uint8_t scratch[SCRATCH_SIZE];

/*
 * The current SETUP, spread into bytes. An 8051 reaches a static byte in one
 * instruction and a struct field behind a pointer in several, and the whole
 * bootloader has to fit below the ISP entry stub.
 */
static uint8_t  req_type;
static uint8_t  req_request;
static uint8_t  req_value_lo;
static uint8_t  req_value_hi;
static uint8_t  req_index_lo;
static uint16_t req_length;

static ep0_state_t  ep0_state;
static ep0_source_t ep0_source;
static uint16_t     ep0_left;
static uint16_t     ep0_index;
static __bit        ep0_fills_wlength;
static __bit        ep0_in_last;

static const __code uint8_t *ep0_code_src;

static uint8_t pending_address;
static uint8_t configuration;

static uint8_t ep0_next_byte(void)
{
    uint16_t index = ep0_index++;

    if (ep0_source == EP0_SRC_CODE) {
        return ep0_code_src[index];
    }
    if (ep0_source == EP0_SRC_SCRATCH) {
        return scratch[index];
    }
    return isp_get_report_byte(index);
}

static void ep0_stall(void)
{
    ep0_state = EP0_IDLE;
    STALL_EP0();
    SET_EP0_OUT_RDY;
}

static void ep0_ack(void)
{
    SET_EP0_OUT_RDY;
    ep0_state = EP0_STATUS_IN;
    SET_EP0_CNT(0);
    SET_EP0_IN_RDY;
}

// arms IN only for a packet the host will read: a zero-length one ends the data stage only short of wLength
static void ep0_in_step(void)
{
    if (ep0_in_last) {
        ep0_state = EP0_STATUS_OUT;
        if (ep0_source == EP0_SRC_REPORT) {
            isp_get_report_end();
        }
        return;
    }

    uint8_t chunk = (ep0_left > EP0_BUF_SIZE) ? EP0_BUF_SIZE : (uint8_t)ep0_left;

    for (uint8_t i = 0; i < chunk; i++) {
        EP0_IN_BUF[i] = ep0_next_byte();
    }

    SET_EP0_CNT(chunk);
    SET_EP0_IN_RDY;

    ep0_left -= chunk;
    ep0_in_last = (chunk < EP0_BUF_SIZE) || (ep0_left == 0 && ep0_fills_wlength);
}

static void ep0_in_begin(ep0_source_t source, uint16_t length)
{
    if (req_length == 0) {
        ep0_ack();
        return;
    }

    ep0_source        = source;
    ep0_index         = 0;
    ep0_left          = (req_length < length) ? req_length : length;
    ep0_fills_wlength = (ep0_left == req_length);
    ep0_in_last       = 0;
    ep0_state         = EP0_IN_DATA;

    SET_EP0_OUT_RDY;
    ep0_in_step();
}

static void ep0_in_code(const __code uint8_t *src, uint16_t length)
{
    ep0_code_src = src;
    ep0_in_begin(EP0_SRC_CODE, length);
}

static void ep0_in_scratch(uint8_t length)
{
    ep0_in_begin(EP0_SRC_SCRATCH, length);
}

static uint8_t string_descriptor(void)
{
    const __code char *src = strings[req_value_lo - 1u];
    uint8_t            len = 2;

    while (*src && len + 2u <= SCRATCH_SIZE) {
        scratch[len++] = (uint8_t)*src++;
        scratch[len++] = 0;
    }

    scratch[0] = len;
    scratch[1] = USB_DESC_STRING;
    return len;
}

static void get_descriptor(void)
{
    uint8_t type = req_value_hi;

    if (type == USB_DESC_DEVICE) {
        ep0_in_code(device_desc, sizeof(device_desc));
    } else if (type == USB_DESC_CONFIGURATION && req_value_lo == 0) {
        ep0_in_code(config_desc, sizeof(config_desc));
    } else if (type == USB_DESC_CLASS_REPORT) {
        ep0_in_code(hid_report_desc, sizeof(hid_report_desc));
    } else if (type == USB_DESC_CLASS_HID) {
        ep0_in_code(&config_desc[HID_DESC_OFFSET], config_desc[HID_DESC_OFFSET]);
    } else if (type == USB_DESC_STRING && req_value_lo == USB_STRING_LANGUAGE_ID) {
        ep0_in_code(langid_desc, sizeof(langid_desc));
    } else if (type == USB_DESC_STRING && req_value_lo <= USB_STRING_SERIAL_NUMBER) {
        ep0_in_scratch(string_descriptor());
    } else {
        ep0_stall();
    }
}

static void standard_request(void)
{
    if (req_type & USB_DIR_IN) {
        if (req_request == USB_REQ_GET_DESCRIPTOR) {
            get_descriptor();
            return;
        }

        // bus-powered, no remote wakeup, and the one endpoint that can halt is EP1 IN
        scratch[0] = 0;
        scratch[1] = 0;

        if (req_request == USB_REQ_GET_STATUS) {
            if (req_index_lo == 0x81u && (EP1CON & _IEP1STL)) {
                scratch[0] = 1;
            }
            ep0_in_scratch(2);
        } else if (req_request == USB_REQ_GET_CONFIGURATION) {
            scratch[0] = configuration;
            ep0_in_scratch(1);
        } else if (req_request == USB_REQ_GET_INTERFACE) {
            ep0_in_scratch(1);
        } else {
            ep0_stall();
        }
        return;
    }

    if (req_request == USB_REQ_SET_ADDRESS) {
        pending_address = req_value_lo;
    } else if (req_request == USB_REQ_SET_CONFIGURATION) {
        if (req_value_lo > 1u || req_value_hi != 0u) {
            ep0_stall();
            return;
        }
        configuration = req_value_lo;
    } else if (req_request == USB_REQ_SET_FEATURE || req_request == USB_REQ_CLEAR_FEATURE) {
        if ((req_type & USB_RECIP_MASK) == USB_RECIP_ENDPT && req_index_lo == 0x81u) {
            if (req_request == USB_REQ_SET_FEATURE) {
                SET_EP1_IN_STALL;
            } else {
                CANCEL_EP1_IN_STALL;
            }
        }
    } else if (req_request != USB_REQ_SET_INTERFACE) {
        ep0_stall();
        return;
    }

    ep0_ack();
}

static void class_request(void)
{
    uint16_t length;

    // SET_IDLE's wValue is a duration, not a report type, so it is answered first
    if (req_request == USB_HID_REQ_SET_IDLE) {
        ep0_ack();
        return;
    }

    if (req_value_hi == REPORT_TYPE_FEATURE) {
        if (req_request == USB_HID_REQ_GET_REPORT && (req_type & USB_DIR_IN)) {
            if (isp_get_report_begin(req_value_lo, &length)) {
                ep0_in_begin(EP0_SRC_REPORT, length);
                return;
            }
        } else if (req_request == USB_HID_REQ_SET_REPORT && !(req_type & USB_DIR_IN)) {
            if (isp_set_report_begin(req_value_lo, req_length)) {
                ep0_state = EP0_OUT_DATA;
                ep0_index = 0;
                ep0_left  = req_length;
                SET_EP0_OUT_RDY;
                return;
            }
        }
    }

    ep0_stall();
}

static void ep0_in_done(void)
{
    if (ep0_state == EP0_IN_DATA) {
        ep0_in_step();
    } else if (ep0_state == EP0_STATUS_IN) {
        ep0_state = EP0_IDLE;
        USBADDR   = pending_address;
    }
}

// a SETUP aborts whatever transfer came before it, so a stale IN completion must not count toward the new one
static void ep0_setup(void)
{
    if ((USBIF2 & _IEP0IF) && ep0_state == EP0_STATUS_IN) {
        ep0_in_done();
    }
    USBIF2 &= (uint8_t)~(_IEP0IF | _OEP0IF);
    ep0_state = EP0_IDLE;

    req_type     = EP0_OUT_BUF[0];
    req_request  = EP0_OUT_BUF[1];
    req_value_lo = EP0_OUT_BUF[2];
    req_value_hi = EP0_OUT_BUF[3];
    req_index_lo = EP0_OUT_BUF[4];
    req_length   = (uint16_t)(EP0_OUT_BUF[6] | ((uint16_t)EP0_OUT_BUF[7] << 8));

    uint8_t type = req_type & USB_TYPE_MASK;

    if (type == USB_TYPE_STANDARD) {
        standard_request();
    } else if (type == USB_TYPE_CLASS && (req_type & USB_RECIP_MASK) == USB_RECIP_IFACE) {
        class_request();
    } else {
        ep0_stall();
    }
}

static void ep0_out_done(void)
{
    if (ep0_state != EP0_OUT_DATA) {
        // the status OUT of an IN transfer, or the host cutting its data stage short
        if (ep0_state == EP0_IN_DATA || ep0_state == EP0_STATUS_OUT) {
            ep0_state = EP0_IDLE;
        }
        SET_EP0_OUT_RDY;
        return;
    }

    uint8_t chunk = (ep0_left > EP0_BUF_SIZE) ? EP0_BUF_SIZE : (uint8_t)ep0_left;

    if (GET_EP0_OUT_CNT() != chunk) {
        ep0_stall();
        return;
    }

    for (uint8_t i = 0; i < chunk; i++) {
        isp_set_report_byte(ep0_index++, EP0_OUT_BUF[i]);
    }

    ep0_left -= chunk;

    if (ep0_left != 0) {
        SET_EP0_OUT_RDY;
        return;
    }

    isp_set_report_end();
    ep0_ack();
}

// the interrupt-enable bits only shape which events raise a flag; IEN1's EUSB stays clear, so nothing vectors
static void usb_attach(void)
{
    USBADDR = 0;
    USBIE1  = (_SETUPIE | _SOFIA | _RESMIE | _SUSPIE | _PBRSTIE);
    USBIE2  = (_OEP0IE | _IEP0IE);
    USBCON  = (_ENUSB | _SW1CON);
}

static void usb_detach(void)
{
    USBADDR = 0;
    USBCON &= (uint8_t)~(_ENUSB | _SW1CON | _SW2CON);
}

static void usb_reset(void)
{
    ep0_state       = EP0_IDLE;
    pending_address = 0;
    configuration   = 0;

    usb_attach();
}

void boot_usb_start(void)
{
    USB_PAGE_ENTER();

    /*
     * The application may have been enumerated a moment ago (sinowisp reaches
     * the bootloader by asking it to jump here), so drop off the bus long
     * enough for the host to notice before enumerating as a different device.
     */
    usb_detach();
    delay_ms(200);

    usb_reset();

    USB_PAGE_LEAVE();
}

void boot_usb_stop(void)
{
    USB_PAGE_ENTER();
    usb_detach();
    USB_PAGE_LEAVE();
}

// Polled, never an ISR: the bootloader shares 0x0003 upwards with the application's vectors.
void boot_usb_poll(void)
{
    USB_PAGE_ENTER();

    uint8_t if1 = USBIF1;

    if (if1 & (_USBRSTIF | _PUPIF)) {
        USBIF1 &= (uint8_t)~(_USBRSTIF | _PUPIF);

        USBCON |= _SWRST;
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
        USBCON &= (uint8_t)~_SWRST;

        usb_reset();
        SET_EP0_OUT_RDY;
        USB_PAGE_LEAVE();
        return;
    }

    if (if1 & (_SOFIF | _SUSPIF | _RESMIF)) {
        USBIF1 &= (uint8_t)~(_SOFIF | _SUSPIF | _RESMIF);
    }

    if (if1 & _SETUPIF) {
        USBIF1 &= (uint8_t)~(_SETUPIF | _OVERIF | _OW);
        ep0_setup();
    }

    // read after the SETUP, which drops the completions of the transfer it aborts
    uint8_t if2 = USBIF2;

    if (if2 & _OEP0IF) {
        USBIF2 &= (uint8_t)~_OEP0IF;
        ep0_out_done();
    }

    if (if2 & _IEP0IF) {
        USBIF2 &= (uint8_t)~_IEP0IF;
        ep0_in_done();
    }

    USB_PAGE_LEAVE();
}
