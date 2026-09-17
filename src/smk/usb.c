#include "usb.h"
#include "usbhw.h"
#include "watchdog.h"
#ifdef ISP_ENABLE
#    include "isp.h"
#endif
#include "usbdef.h"
#include "debug.h"
#include "utils.h"
#include "usbhidreport.h"
#include "console.h"
#include "keyboard.h"
#include "delay.h"
#include <stdint.h>
#include <string.h>

#ifndef USB_VID
#    define USB_VID 0xdead
#endif

#ifndef USB_PID
#    define USB_PID 0xbeef
#endif

#ifndef USB_STR_MANUFACTURER
#    define USB_STR_MANUFACTURER "contact@carlossless.io"
#endif

#ifndef USB_STR_PRODUCT
#    define USB_STR_PRODUCT "SMK Keyboard"
#endif

#ifndef USB_STR_SERIAL
#    define USB_STR_SERIAL "0001"
#endif

#define BCDHID 0x0111 // HID Class Spec Version

#define ISP_CMD_ENTER 0x75

enum usb_string_index {
    USB_STRING_LANGUAGE_ID   = 0,
    USB_STRING_MANUFACTURER  = 1,
    USB_STRING_PRODUCT       = 2,
    USB_STRING_SERIAL_NUMBER = 3,
};

typedef enum {
    EP0_STAGE_IDLE,
    EP0_STAGE_DATA_IN,
    EP0_STAGE_STATUS_OUT,
    EP0_STAGE_DATA_OUT,
    EP0_STAGE_STATUS_IN,
} ep0_stage_t;

typedef enum {
    EP0_OUT_LED,
    EP0_OUT_ISP,
    EP0_OUT_CONSOLE,
} ep0_out_target_t;

const uint8_t hid_report_desc_keyboard[] = {
    // clang-format off
    HID_RI_USAGE_PAGE(8, 0x01),     // Generic Desktop Controls
    HID_RI_USAGE(8, 0x06),          // System Control
    HID_RI_COLLECTION(8, 0x01),     // Application
        // Modifiers (8 bits)
        HID_RI_USAGE_PAGE(8, 0x07),     // Keyboard/Keypad
        HID_RI_USAGE_MINIMUM(8, 0xe0),  // Keyboard Left Control
        HID_RI_USAGE_MAXIMUM(8, 0xe7),  // Keyboard Right Gui
        HID_RI_LOGICAL_MINIMUM(8, 0x00),
        HID_RI_LOGICAL_MAXIMUM(8, 0x01),
        HID_RI_REPORT_SIZE(8, 0x01),
        HID_RI_REPORT_COUNT(8, 0x08),
        HID_RI_INPUT(8, HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),

        // Reserved (1 byte)
        HID_RI_REPORT_SIZE(8, 0x08),
        HID_RI_REPORT_COUNT(8, 0x01),
        HID_RI_INPUT(8, HID_IOF_CONSTANT),

        // Keycodes (6 bytes)
        HID_RI_USAGE_PAGE(8, 0x07),    // Keyboard/Keypad
        HID_RI_USAGE_MINIMUM(8, 0x00),
        HID_RI_USAGE_MAXIMUM(8, 0xFF),
        HID_RI_LOGICAL_MINIMUM(8, 0x00),
        HID_RI_LOGICAL_MAXIMUM(16, 0x00FF),
        HID_RI_REPORT_SIZE(8, 0x08),
        HID_RI_REPORT_COUNT(8, 0x06),
        HID_RI_INPUT(8, HID_IOF_DATA | HID_IOF_ARRAY | HID_IOF_ABSOLUTE),

        // Status LEDs (5 bits)
        HID_RI_USAGE_PAGE(8, 0x08),    // LED
        HID_RI_USAGE_MINIMUM(8, 0x01), // Num Lock
        HID_RI_USAGE_MAXIMUM(8, 0x05), // Kana
        HID_RI_LOGICAL_MINIMUM(8, 0x00),
        HID_RI_LOGICAL_MAXIMUM(8, 0x01),
        HID_RI_REPORT_SIZE(8, 0x01),
        HID_RI_REPORT_COUNT(8, 0x05),
        HID_RI_OUTPUT(8, HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE | HID_IOF_NON_VOLATILE),

        // LED padding (3 bits)
        HID_RI_REPORT_SIZE(8, 0x03),
        HID_RI_REPORT_COUNT(8, 0x01),
        HID_RI_OUTPUT(8, HID_IOF_CONSTANT),
    HID_RI_END_COLLECTION(0),
    // clang-format on
};

const uint8_t hid_report_desc_extra[] = {
    // clang-format off
    HID_RI_USAGE_PAGE(8, 0x01),           // Generic Desktop
    HID_RI_USAGE(8, 0x80),                // System Control
    HID_RI_COLLECTION(8, 0x01),           // Application
        HID_RI_REPORT_ID(8, REPORT_ID_SYSTEM),
        HID_RI_USAGE_MINIMUM(8, 0x81),
        HID_RI_USAGE_MAXIMUM(8, 0x83),
        HID_RI_LOGICAL_MINIMUM(8, 0x00),
        HID_RI_LOGICAL_MAXIMUM(8, 0x01),
        HID_RI_REPORT_SIZE(8, 1),
        HID_RI_REPORT_COUNT(8, 3),
        HID_RI_INPUT(8, HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),
        HID_RI_REPORT_COUNT(8, 5),
        HID_RI_INPUT(8, HID_IOF_CONSTANT | HID_IOF_ARRAY | HID_IOF_ABSOLUTE),
    HID_RI_END_COLLECTION(0),

    HID_RI_USAGE_PAGE(8, 0x0c),           // Consumer
    HID_RI_USAGE(8, 0x01),                // Consumer Control
    HID_RI_COLLECTION(8, 0x01),           // Application
        HID_RI_REPORT_ID(8, REPORT_ID_CONSUMER),
        HID_RI_USAGE_MINIMUM(8, 0x00), // TODO: revise ranges
        HID_RI_USAGE_MAXIMUM(16, 0x023c), // TODO: revise ranges
        HID_RI_LOGICAL_MINIMUM(8, 0x00), // TODO: revise ranges
        HID_RI_LOGICAL_MAXIMUM(16, 0x023c), // TODO: revise ranges
        HID_RI_REPORT_SIZE(8, 16),
        HID_RI_REPORT_COUNT(8, 1),
        HID_RI_INPUT(8, HID_IOF_DATA | HID_IOF_ARRAY | HID_IOF_ABSOLUTE),
    HID_RI_END_COLLECTION(0),

#ifdef ISP_ENABLE
    HID_RI_USAGE_PAGE(16, 0xff00),        // Vendor
    HID_RI_USAGE(8, 0x01),                // Vendor
    HID_RI_COLLECTION(8, 0x01),           // Application
        HID_RI_REPORT_ID(8, REPORT_ID_ISP),
        HID_RI_USAGE_MINIMUM(8, 0x01),
        HID_RI_USAGE_MAXIMUM(8, 0x02),
        HID_RI_LOGICAL_MINIMUM(8, 0x00),
        HID_RI_LOGICAL_MAXIMUM(16, 0x00ff),
        HID_RI_REPORT_SIZE(8, 8),
        HID_RI_REPORT_COUNT(8, 5),
        HID_RI_FEATURE(8, HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),
    HID_RI_END_COLLECTION(0),
#endif

#if DEBUG == 1
    HID_RI_USAGE_PAGE(16, 0xff31),        // Vendor (console page)
    HID_RI_USAGE(8, 0x74),                // Console
    HID_RI_COLLECTION(8, 0x01),           // Application
        HID_RI_REPORT_ID(8, REPORT_ID_CONSOLE),
        HID_RI_USAGE(8, 0x75),            // Console data
        HID_RI_LOGICAL_MINIMUM(8, 0x00),
        HID_RI_LOGICAL_MAXIMUM(16, 0x00ff),
        HID_RI_REPORT_SIZE(8, 0x08),
        HID_RI_REPORT_COUNT(8, CONSOLE_REPORT_SIZE),
        HID_RI_INPUT(8, HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),
    HID_RI_END_COLLECTION(0),
#endif // DEBUG

#ifdef NKRO_ENABLE
    HID_RI_USAGE_PAGE(8, 0x01),           // Generic Desktop
    HID_RI_USAGE(8, 0x06),                // Keyboard
    HID_RI_COLLECTION(8, 0x01),           // Application
        HID_RI_REPORT_ID(8, REPORT_ID_NKRO),
        // Modifiers (8 bits)
        HID_RI_USAGE_PAGE(8, 0x07),     // Keyboard/Keypad
        HID_RI_USAGE_MINIMUM(8, 0xe0),  // Keyboard Left Control
        HID_RI_USAGE_MAXIMUM(8, 0xe7),  // Keyboard Right Gui
        HID_RI_LOGICAL_MINIMUM(8, 0x00),
        HID_RI_LOGICAL_MAXIMUM(8, 0x01),
        HID_RI_REPORT_SIZE(8, 0x01),
        HID_RI_REPORT_COUNT(8, 0x08),
        HID_RI_INPUT(8, HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),

        // NKRO
        HID_RI_USAGE_PAGE(8, 0x07),       // Keyboard/Keypad
        HID_RI_USAGE_MINIMUM(8, 0x00),
        HID_RI_USAGE_MAXIMUM(8, NKRO_REPORT_BITS * 8 - 1),
        HID_RI_LOGICAL_MINIMUM(8, 0x00),
        HID_RI_LOGICAL_MAXIMUM(8, 0x01),
        HID_RI_REPORT_SIZE(8, 1),
        HID_RI_REPORT_COUNT(8, NKRO_REPORT_BITS * 8),
        HID_RI_INPUT(8, HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),
    HID_RI_END_COLLECTION(0),
#endif // NKRO_ENABLE

    // clang-format on
};

usb_desc_device_c usb_desc_device = {
    .bLength            = sizeof(struct usb_desc_device),
    .bDescriptorType    = USB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = USB_DEV_CLASS_PER_INTERFACE,
    .bDeviceSubClass    = USB_DEV_SUBCLASS_PER_INTERFACE,
    .bDeviceProtocol    = USB_DEV_PROTOCOL_PER_INTERFACE,
    .bMaxPacketSize0    = 8,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0000,
    .iManufacturer      = USB_STRING_MANUFACTURER,
    .iProduct           = USB_STRING_PRODUCT,
    .iSerialNumber      = USB_STRING_SERIAL_NUMBER,
    .bNumConfigurations = 1,
};

usb_desc_interface_c usb_desc_interface_main = {
    .bLength            = sizeof(struct usb_desc_interface),
    .bDescriptorType    = USB_DESC_INTERFACE,
    .bInterfaceNumber   = 0,
    .bAlternateSetting  = 0,
    .bNumEndpoints      = 1,
    .bInterfaceClass    = USB_IFACE_CLASS_HID,
    .bInterfaceSubClass = USB_IFACE_SUBCLASS_HID_BOOT,
    .bInterfaceProtocol = USB_IFACE_PROTOCOL_REPORT,
    .iInterface         = 0,
};

usb_desc_hid_c usb_desc_hid_main = {
    .bLength            = sizeof(struct usb_desc_hid),
    .bDescriptorType    = USB_DESC_CLASS_HID,
    .bcdHID             = BCDHID,
    .bCountryCode       = 0,
    .bNumDescriptors    = 1,
    .bDescriptorType1   = USB_DESC_CLASS_REPORT,
    .wDescriptorLength1 = sizeof(hid_report_desc_keyboard),
};

usb_desc_endpoint_c usb_desc_endpoint_main = {
    .bLength          = sizeof(struct usb_desc_endpoint),
    .bDescriptorType  = USB_DESC_ENDPOINT,
    .bEndpointAddress = 1 | USB_DIR_IN,
    .bmAttributes     = USB_XFER_INTERRUPT,
    .wMaxPacketSize   = 16, // 16 bytes
    .bInterval        = 1,  // 1ms
};

usb_desc_interface_c usb_desc_interface_extra = {
    .bLength            = sizeof(struct usb_desc_interface),
    .bDescriptorType    = USB_DESC_INTERFACE,
    .bInterfaceNumber   = 1,
    .bAlternateSetting  = 0,
    .bNumEndpoints      = 1,
    .bInterfaceClass    = USB_IFACE_CLASS_HID,
    .bInterfaceSubClass = USB_IFACE_SUBCLASS_NONE,
    .bInterfaceProtocol = USB_IFACE_PROTOCOL_BOOT,
    .iInterface         = 0,
};

usb_desc_hid_c usb_desc_hid_extra = {
    .bLength            = sizeof(struct usb_desc_hid),
    .bDescriptorType    = USB_DESC_CLASS_HID,
    .bcdHID             = BCDHID,
    .bCountryCode       = 0,
    .bNumDescriptors    = 1,
    .bDescriptorType1   = USB_DESC_CLASS_REPORT,
    .wDescriptorLength1 = sizeof(hid_report_desc_extra),
};

usb_desc_endpoint_c usb_desc_endpoint_extra = {
    .bLength          = sizeof(struct usb_desc_endpoint),
    .bDescriptorType  = USB_DESC_ENDPOINT,
    .bEndpointAddress = 2 | USB_DIR_IN,
    .bmAttributes     = USB_XFER_INTERRUPT,
    .wMaxPacketSize   = 64, // 64 bytes
    .bInterval        = 1,  // 1ms
};

usb_configuration_c usb_config = {
    {
        .bLength             = sizeof(struct usb_desc_configuration),
        .bDescriptorType     = USB_DESC_CONFIGURATION,
        .bNumInterfaces      = 2,
        .bConfigurationValue = 1,
        .iConfiguration      = 0,
        .bmAttributes        = USB_ATTR_RESERVED_1 | USB_ATTR_REMOTE_WAKEUP,
        .bMaxPower           = 250, // 500mA
    },
    {
        {.interface = &usb_desc_interface_main},
        {.hid = &usb_desc_hid_main},
        {.endpoint = &usb_desc_endpoint_main},
        {.interface = &usb_desc_interface_extra},
        {.hid = &usb_desc_hid_extra},
        {.endpoint = &usb_desc_endpoint_extra},
        {0},
    },
};

usb_configuration_set_c usb_configs[] = {
    &usb_config,
};

static usb_desc_langid_c usb_langid = {
    .bLength         = sizeof(struct usb_desc_langid) + sizeof(uint16_t) * 1,
    .bDescriptorType = USB_DESC_STRING,
    .wLANGID         = {/* English (United States) */ 0x0409},
};

usb_ascii_string_c usb_strings[] = {
    [USB_STRING_MANUFACTURER - 1]  = USB_STR_MANUFACTURER,
    [USB_STRING_PRODUCT - 1]       = USB_STR_PRODUCT,
    [USB_STRING_SERIAL_NUMBER - 1] = USB_STR_SERIAL,
};

usb_descriptor_set_c usb_descriptor_set = {
    .device       = &usb_desc_device,
    .config_count = ARRAYSIZE(usb_configs),
    .configs      = usb_configs,
    .string_count = ARRAYSIZE(usb_strings),
    .strings      = usb_strings,
};

// interrupt handlers
static void usb_setup_irq(void);
static void usb_ep0_out_irq(void);
static void usb_ep0_in_irq(void);

static void ep0_stall(void);
static void ep0_ack(void);
static void ep0_send(uint8_t *src, uint16_t len, uint16_t max_len);
static void ep0_send_next(void);
static void ep0_receive(ep0_out_target_t target, uint16_t len);
static void set_ep0_in_buffer(uint8_t *src, uint8_t len);
static void get_ep0_out_buffer(uint8_t *dest);

// request handlers
static void usb_standard_request(struct usb_req_setup *req);
static void usb_hid_request(struct usb_req_setup *req);
static void usb_get_status_handler(struct usb_req_setup *req);
static void usb_feature_handler(struct usb_req_setup *req, bool set);
static void usb_set_address_handler(struct usb_req_setup *req);
static void usb_set_configuration_handler(struct usb_req_setup *req);
static void usb_get_interface_handler(struct usb_req_setup *req);
static void usb_set_interface_handler(struct usb_req_setup *req);
static void usb_get_descriptor_handler(struct usb_req_setup *req);
static void usb_hid_set_report_handler(struct usb_req_setup *req);

/**
 * 0.5KB of general purpose scratch RAM.
 */
uint8_t scratch[512];

// usb state
usb_device_state_t usb_device_state;
uint8_t            received_usb_addr;
uint8_t            active_configuration;
uint8_t            interface0_protocol;
uint8_t            interface1_protocol;
// Host-controlled remote-wakeup enable, per SET/CLEAR_FEATURE.
static __bit usb_remote_wakeup;
__bit        usb_suspended;
#ifdef ISP_ENABLE
static __bit usb_isp_requested;
static __bit ep0_isp_pending;
#endif
uint8_t idle_time;

static ep0_stage_t      ep0_stage;
static ep0_out_target_t ep0_out_target;
static uint8_t          ep0_out_len;
static uint8_t         *ep0_in_src;
static uint16_t         ep0_in_left;
static __bit            ep0_in_fills_wlength;
static __bit            ep0_in_done;
static __bit            ep0_address_pending;

#define ENUM_QUIET_MS   500
#define ENUM_NO_HOST_MS 500
#define ENUM_GIVE_UP_MS 4000

static volatile uint16_t enum_quiet_ticks;
static volatile bool     enum_seen;

static void usb_reset_state(void)
{
    usb_device_state     = USB_DEVICE_STATE_DEFAULT;
    received_usb_addr    = 0;
    active_configuration = 0;
    interface0_protocol  = 0;
    interface1_protocol  = 0;
    usb_remote_wakeup    = 0;
    usb_suspended        = 0;
    idle_time            = 0;
#ifdef ISP_ENABLE
    ep0_isp_pending = 0;
#endif
    ep0_address_pending = 0;
    ep0_stage           = EP0_STAGE_IDLE;
}

void usb_init()
{
    usb_reset_state();

    enum_quiet_ticks = 0;
    enum_seen        = false;

    usb_hw_init();
}

void usb_deinit()
{
    usb_hw_deinit();
    usb_reset_state();
}

void usb_send_report(__xdata report_keyboard_t *report)
{
    if (!usb_is_configured()) {
        return;
    }
    usb_hw_ep1_in_send(report->raw, KEYBOARD_REPORT_SIZE);
}

void usb_send_nkro(__xdata report_nkro_t *report)
{
    if (!usb_is_configured()) {
        return;
    }
    usb_hw_ep2_in_send(report->raw, NKRO_REPORT_SIZE);
}

void usb_send_extra(__xdata report_extra_t *report)
{
    if (!usb_is_configured()) {
        return;
    }
    usb_hw_ep2_in_send(report->raw, EXTRA_REPORT_SIZE);
}

void usb_wait_for_enumeration(void)
{
    for (uint16_t ms = 0; ms < ENUM_GIVE_UP_MS; ms++) {
        watchdog_kick();

        uint16_t quiet;
        __critical
        {
            quiet = enum_quiet_ticks;
        }

        if (enum_seen) {
            if (quiet == 0) {
                return;
            }
        } else if (ms >= ENUM_NO_HOST_MS) {
            return; // no SETUP ever arrived: nothing is driving the bus
        }

        delay_ms(1);
    }
}

// not from the ISR: the bootloader never RETIs and reboots by jumping, so an in-service latch outlives it and kills USB.
void usb_task(void)
{
#ifdef ISP_ENABLE
    if (usb_isp_requested) {
        isp_jump();
    }
#endif
}

#if DEBUG == 1
bool usb_console_ready(void)
{
    return usb_is_configured() && usb_hw_ep2_in_free();
}

void usb_console_send(const __xdata uint8_t *data, uint8_t len)
{
    usb_hw_console_send(data, len);
}
#endif

uint8_t usb_device_state_get_protocol()
{
    return interface0_protocol;
}

bool usb_is_configured(void)
{
    return usb_device_state == USB_DEVICE_STATE_CONFIGURED;
}

void usb_irq_dispatch(void)
{
    uint8_t if1 = USBIF1;

    if (if1 & (_USBRSTIF | _PUPIF)) {
        USBIF1 &= ~(_USBRSTIF | _PUPIF);

        USBCON |= _SWRST;
        _nop_();
        _nop_();
        _nop_();
        _nop_();
        _nop_();
        _nop_();
        USBCON &= ~_SWRST;

        usb_init();
        SET_EP0_OUT_RDY;
        return;
    }

    if (if1 & _SOFIF) {
        USBIF1 &= ~_SOFIF;
        if (enum_quiet_ticks) {
            enum_quiet_ticks--;
        }
        usb_suspended = 0; // a SOF means the host is driving the bus again
    }

    if (if1 & _RESMIF) {
        USBIF1 &= ~_RESMIF;
    }

    if (if1 & _SUSPIF) {
        USBIF1 &= ~_SUSPIF;
        if (usb_device_state == USB_DEVICE_STATE_CONFIGURED) {
            usb_suspended = 1;
        }
    }

    if (if1 & _SETUPIF) {
        USBIF1 &= ~_SETUPIF;
        USBIF1 &= ~(_OVERIF | _OW);
        enum_quiet_ticks = ENUM_QUIET_MS;
        enum_seen        = true;
        usb_setup_irq();
    }

    uint8_t if2 = USBIF2;

    if (if2 & _OEP0IF) {
        USBIF2 &= ~_OEP0IF;
        usb_ep0_out_irq();
    }

    if (if2 & _IEP0IF) {
        USBIF2 &= ~_IEP0IF;
        usb_ep0_in_irq();
    }

    if (if2 & _IEP2IF) {
        USBIF2 &= ~_IEP2IF;
        usb_hw_ep2_in_complete();
    }

    if (if2 & _IEP1IF) {
        USBIF2 &= ~_IEP1IF;
        usb_hw_ep1_in_complete();
    }
}

static void usb_setup_irq(void)
{
    struct usb_req_setup req;

    if ((USBIF2 & _IEP0IF) && ep0_stage == EP0_STAGE_STATUS_IN) {
        usb_ep0_in_irq();
    }
    USBIF2 &= ~(_IEP0IF | _OEP0IF);
    ep0_stage           = EP0_STAGE_IDLE;
    ep0_address_pending = 0;
#ifdef ISP_ENABLE
    ep0_isp_pending = 0;
#endif

    get_ep0_out_buffer((uint8_t *)&req);

    switch (req.bmRequestType & USB_TYPE_MASK) {
        case USB_TYPE_STANDARD:
            usb_standard_request(&req);
            break;

        case USB_TYPE_CLASS:
            usb_hid_request(&req);
            break;

        default:
            ep0_stall();
            break;
    }
}

static void usb_ep0_out_irq(void)
{
    SET_EP0_OUT_RDY;

    if (ep0_stage == EP0_STAGE_DATA_OUT) {
        if (GET_EP0_OUT_CNT() != ep0_out_len) {
            ep0_stall();
            return;
        }

        switch (ep0_out_target) {
            case EP0_OUT_LED:
                keyboard_set_led_state(EP0_OUT_BUF[0]);
                break;

#ifdef ISP_ENABLE
            case EP0_OUT_ISP:
                if (EP0_OUT_BUF[0] == REPORT_ID_ISP && EP0_OUT_BUF[1] == ISP_CMD_ENTER) {
                    ep0_isp_pending = 1;
                }
                break;
#endif

#if DEBUG == 1
            case EP0_OUT_CONSOLE:
                console_notify_attached(); // host tool is listening; start draining
                break;
#endif

            default:
                break;
        }
        ep0_ack();
    } else if (ep0_stage == EP0_STAGE_DATA_IN || ep0_stage == EP0_STAGE_STATUS_OUT) {
        ep0_stage = EP0_STAGE_IDLE;
    }
}

static void usb_ep0_in_irq(void)
{
    if (ep0_stage == EP0_STAGE_DATA_IN) {
        ep0_send_next();
    } else if (ep0_stage == EP0_STAGE_STATUS_IN) {
        ep0_stage = EP0_STAGE_IDLE;

        if (ep0_address_pending) {
            ep0_address_pending = 0;
            USBADDR             = received_usb_addr;
        }

#ifdef ISP_ENABLE
        if (ep0_isp_pending) {
            ep0_isp_pending   = 0;
            usb_isp_requested = 1;
        }
#endif
    }
}

static void ep0_stall(void)
{
    ep0_stage = EP0_STAGE_IDLE;
    STALL_EP0();
    SET_EP0_OUT_RDY;
}

static void ep0_ack(void)
{
    SET_EP0_OUT_RDY;
    ep0_stage = EP0_STAGE_STATUS_IN;
    SET_EP0_CNT(0);
    SET_EP0_IN_RDY;
}

static void ep0_send(uint8_t *src, uint16_t len, uint16_t max_len)
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
    ep0_stage            = EP0_STAGE_DATA_IN;

    SET_EP0_OUT_RDY;
    ep0_send_next();
}

static void ep0_send_next(void)
{
    if (ep0_in_done) {
        ep0_stage = EP0_STAGE_STATUS_OUT;
        return;
    }

    uint8_t len = (ep0_in_left > EP0_BUF_SIZE) ? EP0_BUF_SIZE : (uint8_t)ep0_in_left;

    set_ep0_in_buffer(ep0_in_src, len);
    SET_EP0_CNT(len);
    SET_EP0_IN_RDY;

    ep0_in_src += len;
    ep0_in_left -= len;
    ep0_in_done = (len < EP0_BUF_SIZE) || (ep0_in_left == 0 && ep0_in_fills_wlength);
}

static void ep0_receive(ep0_out_target_t target, uint16_t len)
{
    if (len == 0 || len > EP0_BUF_SIZE) {
        ep0_stall();
        return;
    }

    ep0_out_target = target;
    ep0_out_len    = len;
    ep0_stage      = EP0_STAGE_DATA_OUT;
    SET_EP0_OUT_RDY;
}

static void set_ep0_in_buffer(uint8_t *src, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        EP0_IN_BUF[i] = src[i];
    }
}

static void get_ep0_out_buffer(uint8_t *dest)
{
    for (uint8_t i = 0; i < EP0_BUF_SIZE; i++) {
        dest[i] = EP0_OUT_BUF[i];
    }
}

// request handlers

static void usb_standard_request(struct usb_req_setup *req)
{
    bool in = (req->bmRequestType & USB_DIR_MASK) == USB_DIR_IN;

    switch (req->bRequest) {
        case USB_REQ_GET_STATUS:
            if (in) {
                usb_get_status_handler(req);
                return;
            }
            break;

        case USB_REQ_CLEAR_FEATURE:
        case USB_REQ_SET_FEATURE:
            if (!in) {
                usb_feature_handler(req, req->bRequest == USB_REQ_SET_FEATURE);
                return;
            }
            break;

        case USB_REQ_SET_ADDRESS:
            if (!in && (req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_DEVICE) {
                usb_set_address_handler(req);
                return;
            }
            break;

        case USB_REQ_GET_DESCRIPTOR:
            if (in) {
                usb_get_descriptor_handler(req);
                return;
            }
            break;

        case USB_REQ_GET_CONFIGURATION:
            if (in && (req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_DEVICE) {
                scratch[0] = active_configuration;
                ep0_send(scratch, 1, req->wLength);
                return;
            }
            break;

        case USB_REQ_SET_CONFIGURATION:
            if (!in && (req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_DEVICE) {
                usb_set_configuration_handler(req);
                return;
            }
            break;

        case USB_REQ_GET_INTERFACE:
            if (in && (req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_IFACE) {
                usb_get_interface_handler(req);
                return;
            }
            break;

        case USB_REQ_SET_INTERFACE:
            if (!in && (req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_IFACE) {
                usb_set_interface_handler(req);
                return;
            }
            break;

        default:
            break;
    }

    ep0_stall();
}

static void usb_hid_request(struct usb_req_setup *req)
{
    uint8_t iface = req->wIndex & 0xff;

    if ((req->bmRequestType & USB_RECIP_MASK) != USB_RECIP_IFACE || iface > 1) {
        ep0_stall();
        return;
    }

    bool in = (req->bmRequestType & USB_DIR_MASK) == USB_DIR_IN;

    switch (req->bRequest) {
        case USB_HID_REQ_SET_REPORT:
            if (!in) {
                usb_hid_set_report_handler(req);
                return;
            }
            break;

        case USB_HID_REQ_SET_IDLE:
            if (!in) {
                // TODO: finish implementaiton to use the set idle time
                idle_time = req->wValue >> 8;
                ep0_ack();
                return;
            }
            break;

        case USB_HID_REQ_GET_IDLE:
            if (in) {
                scratch[0] = idle_time;
                ep0_send(scratch, 1, req->wLength);
                return;
            }
            break;

        case USB_HID_REQ_SET_PROTOCOL:
            if (!in && req->wValue <= USB_PROTOCOL_REPORT) {
                if (iface == 0) {
                    interface0_protocol = req->wValue;
                } else {
                    interface1_protocol = req->wValue;
                }
                ep0_ack();
                return;
            }
            break;

        case USB_HID_REQ_GET_PROTOCOL:
            if (in) {
                scratch[0] = (iface == 0) ? interface0_protocol : interface1_protocol;
                ep0_send(scratch, 1, req->wLength);
                return;
            }
            break;

        default:
            break;
    }

    ep0_stall();
}

static void usb_get_status_handler(struct usb_req_setup *req)
{
    scratch[0] = 0;
    scratch[1] = 0;

    switch (req->bmRequestType & USB_RECIP_MASK) {
        case USB_RECIP_DEVICE:
            if (usb_remote_wakeup) {
                scratch[0] = 0x02;
            }
            break;

        case USB_RECIP_IFACE:
            if (!usb_is_configured() || req->wIndex > 1) {
                ep0_stall();
                return;
            }
            break;

        case USB_RECIP_ENDPT:
            switch (req->wIndex) {
                case 0x00:
                case 0x80:
                    break;

                case 0x81:
                    if (!usb_is_configured()) {
                        ep0_stall();
                        return;
                    }
                    scratch[0] = (EP1CON & _IEP1STL) ? 1 : 0;
                    break;

                case 0x82:
                    if (!usb_is_configured()) {
                        ep0_stall();
                        return;
                    }
                    scratch[0] = (EP2CON & _IEP2STL) ? 1 : 0;
                    break;

                default:
                    ep0_stall();
                    return;
            }
            break;

        default:
            ep0_stall();
            return;
    }

    ep0_send(scratch, 2, req->wLength);
}

static void usb_feature_handler(struct usb_req_setup *req, bool set)
{
    switch (req->bmRequestType & USB_RECIP_MASK) {
        case USB_RECIP_DEVICE:
            if (req->wValue == USB_FEAT_DEVICE_REMOTE_WAKEUP) {
                usb_remote_wakeup = set;
                ep0_ack();
                return;
            }
            break;

        case USB_RECIP_ENDPT:
            if (req->wValue != USB_FEAT_ENDPOINT_HALT) {
                break;
            }

            if (req->wIndex == 0x00 || req->wIndex == 0x80) {
                ep0_ack();
                return;
            }

            if (!usb_is_configured()) {
                break;
            }

            if (req->wIndex == 0x81) {
                if (set) {
                    SET_EP1_IN_STALL;
                } else {
                    EP1CON &= ~(_IEP1STL | _IEP1DTG);
                }
                ep0_ack();
                return;
            }

            if (req->wIndex == 0x82) {
                if (set) {
                    SET_EP2_IN_STALL;
                } else {
                    EP2CON &= ~(_IEP2STL | _IEP2DTG);
                }
                ep0_ack();
                return;
            }
            break;

        default:
            break;
    }

    ep0_stall();
}

static void usb_set_address_handler(struct usb_req_setup *req)
{
    if (req->wValue > 127) {
        ep0_stall();
        return;
    }

    received_usb_addr   = req->wValue;
    ep0_address_pending = 1;

    if (received_usb_addr != 0) {
        usb_device_state = USB_DEVICE_STATE_ADDRESSED;
    } else {
        usb_device_state = USB_DEVICE_STATE_DEFAULT;
    }

    ep0_ack();
}

static void usb_set_configuration_handler(struct usb_req_setup *req)
{
    // USBADDR does not read back on every part, so test the address we assigned.
    if (!received_usb_addr || req->wValue > 1) {
        ep0_stall();
        return;
    }

    active_configuration = req->wValue;

    if (active_configuration != 0) {
        usb_device_state = USB_DEVICE_STATE_CONFIGURED;
    } else {
        usb_device_state = USB_DEVICE_STATE_ADDRESSED;
    }

    EP1CON &= ~(_IEP1STL | _IEP1DTG);
    EP2CON &= ~(_IEP2STL | _IEP2DTG);

    ep0_ack();
}

static void usb_get_interface_handler(struct usb_req_setup *req)
{
    if (!usb_is_configured() || req->wIndex > 1) {
        ep0_stall();
        return;
    }

    scratch[0] = 0;
    ep0_send(scratch, 1, req->wLength);
}

static void usb_set_interface_handler(struct usb_req_setup *req)
{
    if (!usb_is_configured() || req->wIndex > 1 || req->wValue != 0) {
        ep0_stall();
        return;
    }

    ep0_ack();
}

static void usb_get_descriptor_handler(struct usb_req_setup *req)
{
    uint8_t *addr = NULL;
    uint16_t length;

    __xdata uint8_t      *buf   = scratch;
    uint8_t               type  = req->wValue >> 8;
    uint8_t               index = req->wValue & 0xff;
    usb_descriptor_set_c *set   = &usb_descriptor_set;

#define APPEND(addr, length)                          \
    do {                                              \
        for (uint16_t _i = 0; _i < (length); _i++) {  \
            buf[_i] = ((__code uint8_t *)(addr))[_i]; \
        }                                             \
        buf += (length);                              \
    } while (0)

#define APPEND_DESC(desc) APPEND(desc, (desc)->bLength)

    if (type == USB_DESC_DEVICE) {
        APPEND_DESC(set->device);

        addr   = scratch;
        length = scratch[0];
    } else if (type == USB_DESC_CONFIGURATION && index < set->config_count) {
        usb_configuration_c                   *config      = set->configs[index];
        __xdata struct usb_desc_configuration *config_desc = (__xdata struct usb_desc_configuration *)buf;
        __code const union usb_config_item    *config_item = &config->items[0];

        APPEND_DESC(&config->desc);

        do {
            APPEND_DESC(config_item->generic);
        } while ((++config_item)->generic);

        // Fix up wTotalLength so we don't need to calculate it explicitly.
        if (config_desc->wTotalLength == 0) {
            config_desc->wTotalLength = (uint16_t)(buf - scratch);
        }

        addr   = scratch;
        length = config_desc->wTotalLength;
    } else if (type == USB_DESC_STRING && index == USB_STRING_LANGUAGE_ID) {
        APPEND_DESC(&usb_langid);

        addr   = scratch;
        length = scratch[0];
    } else if (type == USB_DESC_STRING && index - 1 < set->string_count) {
        __code const char *string = set->strings[index - 1];

        *buf++ = 2;               // bLength
        *buf++ = USB_DESC_STRING; // bDescriptorType

        while (*string) {
            *buf++ = *string++;
            *buf++ = 0;
            scratch[0] += 2;
        }

        addr   = scratch;
        length = scratch[0];
    } else if (type == USB_DESC_CLASS_REPORT) {
        uint8_t iface_index = req->wIndex;

        if (iface_index == 0) {
            addr   = (uint8_t *)hid_report_desc_keyboard;
            length = sizeof(hid_report_desc_keyboard);
        } else if (iface_index == 1) {
            addr   = (uint8_t *)hid_report_desc_extra;
            length = sizeof(hid_report_desc_extra);
        } else {
            ep0_stall();
            return;
        }
    } else if (type == USB_DESC_CLASS_HID) {
        uint8_t iface_index = req->wIndex;

        if (iface_index == 0) {
            APPEND_DESC(&usb_desc_hid_main);

            addr   = scratch;
            length = scratch[0];
        } else if (iface_index == 1) {
            APPEND_DESC(&usb_desc_hid_extra);

            addr   = scratch;
            length = scratch[0];
        } else {
            ep0_stall();
            return;
        }
    } else {
        ep0_stall();
        return;
    }

    ep0_send(addr, length, req->wLength);
}

static void usb_hid_set_report_handler(struct usb_req_setup *req)
{
    uint8_t report_type = req->wValue >> 8;
    uint8_t report_id   = req->wValue & 0xff;
    uint8_t iface       = req->wIndex & 0xff;

    (void)report_id;

    if (report_type == REPORT_TYPE_OUTPUT && iface == 0 && req->wLength == 1) {
        ep0_receive(EP0_OUT_LED, req->wLength);
        return;
    }

    if (report_type == REPORT_TYPE_FEATURE && iface == 1) {
#ifdef ISP_ENABLE
        if (report_id == REPORT_ID_ISP) {
            ep0_receive(EP0_OUT_ISP, req->wLength);
            return;
        }
#endif
#if DEBUG == 1
        if (report_id == REPORT_ID_CONSOLE) {
            ep0_receive(EP0_OUT_CONSOLE, req->wLength);
            return;
        }
#endif
    }

    ep0_stall();
}
