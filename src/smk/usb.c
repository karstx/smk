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

enum usb_string_index {
    USB_STRING_LANGUAGE_ID   = 0,
    USB_STRING_MANUFACTURER  = 1,
    USB_STRING_PRODUCT       = 2,
    USB_STRING_SERIAL_NUMBER = 3,
};

typedef enum {
    USB_EP0_STATE_DEFAULT     = 0x00,
    USB_EP0_STATE_IN_DATA     = 0x01,
    USB_EP0_STATE_RECV_STATUS = 0x02,
    USB_EP0_STATE_LED         = 0x04,
    USB_EP0_STATE_ISP         = 0x05,
    USB_EP0_STATE_CONSOLE     = 0x06,
} usb_ep0_state_t;

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
    .bcdUSB             = 0x0110,
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
static void usb_setup_irq();
static void usb_ep0_out_irq();
static void usb_ep0_in_irq();

// buffer utils
static void setup_ep0_in_xfer(uint8_t *src, uint16_t len);
static void step_ep0_in_xfer();
static void set_ep0_in_buffer(uint8_t *src, uint8_t len);
static void get_ep0_out_buffer(uint8_t *dest);

// request handlers
static void usb_clear_remote_wakeup_handler(struct usb_req_setup *req);
static void usb_clear_endpoint_halt_handler(struct usb_req_setup *req);
static void usb_set_remote_wakeup_handler(struct usb_req_setup *req);
static void usb_set_endpoint_halt_handler(struct usb_req_setup *req);
static void usb_set_address_handler(struct usb_req_setup *req);
static void usb_set_configuration_handler(struct usb_req_setup *req);
static void usb_set_interface_handler(struct usb_req_setup *req);
static void usb_set_descriptor_handler();
static void usb_get_device_status_handler(struct usb_req_setup *req);
static void usb_get_interface_status_handler(struct usb_req_setup *req);
static void usb_get_endpoint_status_handler(struct usb_req_setup *req);
static void usb_get_descriptor_handler(struct usb_req_setup *req);
static void usb_get_configuration_handler();
static void usb_get_interface_handler();
static void usb_hid_get_report_handler();
static void usb_hid_set_report_handler(struct usb_req_setup *req);
static void usb_hid_set_idle_handler(struct usb_req_setup *req);
static void usb_hid_get_idle_handler();
static void usb_hid_set_protocol_handler(struct usb_req_setup *req);
static void usb_hid_get_protocol_handler(struct usb_req_setup *req);

/**
 * 0.5KB of general purpose scratch RAM.
 */
uint8_t scratch[512];

uint16_t ep0_xfer_bytes_left;
uint8_t *ep0_xfer_src;

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
#endif
uint8_t         idle_time;
usb_ep0_state_t usb_ep0_state;

#define ENUM_QUIET_MS   500
#define ENUM_NO_HOST_MS 500
#define ENUM_GIVE_UP_MS 4000

static uint16_t enum_quiet_ticks;
static bool     enum_seen;

void usb_init()
{
    usb_device_state     = USB_DEVICE_STATE_DEFAULT;
    received_usb_addr    = 0;
    active_configuration = 0;
    interface0_protocol  = 0;
    interface1_protocol  = 0;
    usb_remote_wakeup    = 0;
    usb_suspended        = 0;
    usb_ep0_state        = USB_EP0_STATE_DEFAULT;
    idle_time            = 0;

    ep0_xfer_bytes_left = 0;
    ep0_xfer_src        = 0;

    enum_quiet_ticks = 0;
    enum_seen        = false;

    usb_hw_init();
}

void usb_deinit()
{
    usb_device_state     = USB_DEVICE_STATE_DEFAULT;
    received_usb_addr    = 0;
    active_configuration = 0;
    interface0_protocol  = 0;
    interface1_protocol  = 0;
    usb_remote_wakeup    = 0;
    usb_suspended        = 0;
    usb_ep0_state        = USB_EP0_STATE_DEFAULT;
    idle_time            = 0;

    usb_hw_deinit();
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

        if (enum_seen) {
            if (enum_quiet_ticks == 0) {
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

static void usb_setup_irq()
{
    struct usb_req_setup req;

    __critical
    {
        get_ep0_out_buffer((uint8_t *)&req);
    }

    uint8_t type    = req.bmRequestType;
    uint8_t request = req.bRequest;

    switch (type) {
        case (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE):
            switch (request) {
                case USB_REQ_CLEAR_FEATURE:
                    usb_clear_remote_wakeup_handler(&req);
                    break;

                case USB_REQ_SET_FEATURE:
                    usb_set_remote_wakeup_handler(&req);
                    break;

                case USB_REQ_SET_ADDRESS:
                    usb_set_address_handler(&req);
                    break;

                case USB_REQ_SET_DESCRIPTOR:
                    usb_set_descriptor_handler();
                    break;

                case USB_REQ_SET_CONFIGURATION:
                    usb_set_configuration_handler(&req);
                    break;

                default:
                    STALL_EP0();
                    return;
            }

            break;

        case (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE):
            switch (request) {
                case USB_REQ_GET_STATUS:
                    usb_get_device_status_handler(&req);
                    break;

                case USB_REQ_GET_DESCRIPTOR:
                    usb_get_descriptor_handler(&req);
                    break;

                case USB_REQ_GET_CONFIGURATION:
                    usb_get_configuration_handler();
                    break;

                default:
                    STALL_EP0();
                    return;
            }

            break;

        case (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_IFACE):
            switch (request) {
                case USB_REQ_SET_INTERFACE:
                    usb_set_interface_handler(&req);
                    break;

                default:
                    STALL_EP0();
                    return;
            }

            break;

        case (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_IFACE):
            switch (request) {
                case USB_REQ_GET_STATUS:
                    usb_get_interface_status_handler(&req);
                    break;

                case USB_REQ_GET_DESCRIPTOR:
                    usb_get_descriptor_handler(&req);
                    break;

                case USB_REQ_GET_INTERFACE:
                    usb_get_interface_handler();
                    break;

                default:
                    STALL_EP0();
                    return;
            }

            break;

        case (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPT):
            switch (request) {
                case USB_REQ_CLEAR_FEATURE:
                    usb_clear_endpoint_halt_handler(&req);
                    break;

                case USB_REQ_SET_FEATURE:
                    usb_set_endpoint_halt_handler(&req);
                    break;

                default:
                    STALL_EP0();
                    return;
            }

            break;

        case (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_ENDPT):
            switch (request) {
                case USB_REQ_GET_STATUS:
                    usb_get_endpoint_status_handler(&req);
                    break;

                default:
                    STALL_EP0();
                    return;
            }

            break;

        case (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_IFACE):
            switch (request) {
                case USB_HID_REQ_SET_REPORT:
                    usb_hid_set_report_handler(&req);
                    break;

                case USB_HID_REQ_SET_IDLE:
                    usb_hid_set_idle_handler(&req);
                    break;

                case USB_HID_REQ_SET_PROTOCOL:
                    usb_hid_set_protocol_handler(&req);
                    break;

                default:
                    STALL_EP0();
                    return;
            }

            break;

        case (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_IFACE):
            switch (request) {
                case USB_HID_REQ_GET_REPORT:
                    usb_hid_get_report_handler();
                    break;

                case USB_HID_REQ_GET_IDLE:
                    usb_hid_get_idle_handler();
                    break;

                case USB_HID_REQ_GET_PROTOCOL:
                    usb_hid_get_protocol_handler(&req);
                    break;

                default:
                    STALL_EP0();
                    return;
            }

            break;

        default:
            STALL_EP0();
            return;
    }
}

void usb_irq_dispatch(void)
{
    uint8_t temp_usbif1 = USBIF1;
    uint8_t temp_usbif2 = USBIF2;

    if (temp_usbif1 != 0x00) {
        if (temp_usbif1 & _SOFIF) {
            USBIF1 &= ~_SOFIF;
            if (enum_quiet_ticks) {
                enum_quiet_ticks--;
            }
            usb_suspended = 0; // a SOF means the host is driving the bus again
        } else {
            USBIF1 &= ~(_SETUPIF);      // Clear SETUPIF
            USBIF1 &= ~(_OVERIF | _OW); // SETUP OVERIF & OW

            if (temp_usbif1 & _PUPIF) {
                USBIF1 &= ~_PUPIF;
                usb_init();
            } else if (temp_usbif1 & _SETUPIF) {
                USBIF1 &= ~_SETUPIF;
                USBIF2 &= ~_IEP0IF;
                enum_quiet_ticks = ENUM_QUIET_MS;
                enum_seen        = true;
                usb_setup_irq();
            } else if (temp_usbif1 & _RESMIF) { // RESMIF
                USBIF1 &= ~_RESMIF;
            } else if (temp_usbif1 & _SUSPIF) { // SUSPIF
                USBIF1 &= ~_SUSPIF;
                if (usb_device_state == USB_DEVICE_STATE_CONFIGURED) {
                    usb_suspended = 1;
                }
            } else if (temp_usbif1 & _USBRSTIF) { // BUSRSTIF
                USBIF1 &= ~_USBRSTIF;

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
            }
        }
    } else if (temp_usbif2 != 0x00) {
        if (temp_usbif2 & _OEP2IF) {
            USBIF2 &= ~_OEP2IF;
            SET_EP2_OUT_RDY;
        } else if (temp_usbif2 & _OEP1IF) {
            USBIF2 &= ~_OEP1IF;
        } else if (temp_usbif2 & _OEP0IF) {
            USBIF2 &= ~_OEP0IF;
            usb_ep0_out_irq();
            SET_EP0_OUT_RDY;
        } else if (temp_usbif2 & _IEP2IF) {
            USBIF2 &= ~_IEP2IF;
            usb_hw_ep2_in_complete();
        } else if (temp_usbif2 & _IEP1IF) {
            USBIF2 &= ~_IEP1IF;
            usb_hw_ep1_in_complete();
        } else if (temp_usbif2 & _IEP0IF) {
            USBIF2 &= ~_IEP0IF;
            usb_ep0_in_irq();
        }
    }
}

// request handlers

static void usb_set_address_handler(struct usb_req_setup *req)
{
    usb_ep0_state = USB_EP0_STATE_DEFAULT;

    if (req->wValue != 0) {
        usb_device_state = USB_DEVICE_STATE_ADDRESSED;
    } else {
        usb_device_state = USB_DEVICE_STATE_DEFAULT;
    }

    received_usb_addr = req->wValue;

    CLEAR_EP0_CNT;
    SET_EP0_OUT_STALL;
    SET_EP0_IN_RDY;
}

static void usb_clear_remote_wakeup_handler(struct usb_req_setup *req)
{
    usb_ep0_state = USB_EP0_STATE_DEFAULT;

    if ((req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_DEVICE) {
        if (req->wValue == USB_FEAT_DEVICE_REMOTE_WAKEUP) {
            usb_remote_wakeup = 0;
        }
    }

    CLEAR_EP0_CNT;
    SET_EP0_IN_RDY;
}

static void usb_set_remote_wakeup_handler(struct usb_req_setup *req)
{
    usb_ep0_state = USB_EP0_STATE_DEFAULT;

    if ((req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_DEVICE) {
        if (req->wValue == USB_FEAT_DEVICE_REMOTE_WAKEUP) {
            usb_remote_wakeup = 1;
        }
    }

    CLEAR_EP0_CNT;
    SET_EP0_IN_RDY;
}

static void usb_clear_endpoint_halt_handler(struct usb_req_setup *req)
{
    usb_ep0_state = USB_EP0_STATE_DEFAULT;

    if ((req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_ENDPT) {
        if (req->wValue == USB_FEAT_ENDPOINT_HALT) {
            if (req->wIndex == 0) {
                CANCEL_EP0_IN_STALL;
                CANCEL_EP0_OUT_STALL;
            } else if (req->wIndex == 0x81) {
                CANCEL_EP1_IN_STALL;
            } else if (req->wIndex == 0x82) {
                CANCEL_EP2_IN_STALL;
            } else {
                STALL_EP0();
                return;
            }
        }
    }

    CLEAR_EP0_CNT;
    SET_EP0_IN_RDY;
}

static void usb_set_endpoint_halt_handler(struct usb_req_setup *req)
{
    usb_ep0_state = USB_EP0_STATE_DEFAULT;

    if ((req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_ENDPT) {
        if (req->wIndex == 0x81) {
            SET_EP1_IN_STALL;
        } else if (req->wIndex == 0x82) {
            SET_EP2_IN_STALL;
        } else {
            STALL_EP0();
        }
    } else {
        STALL_EP0();
        return;
    }

    CLEAR_EP0_CNT;
    SET_EP0_IN_RDY;
}

static void usb_set_configuration_handler(struct usb_req_setup *req)
{
    usb_ep0_state = USB_EP0_STATE_DEFAULT;

    // USBADDR does not read back on every part, so test the address we assigned.
    if (received_usb_addr) {
        if ((req->wValue == 0) || (req->wValue == 1)) {
            active_configuration = req->wValue;

            if (active_configuration != 0) {
                usb_device_state = USB_DEVICE_STATE_CONFIGURED;
            } else {
                usb_device_state = USB_DEVICE_STATE_ADDRESSED;
            }

            CLEAR_EP0_CNT;
            SET_EP0_IN_RDY;
        } else {
            STALL_EP0();
        }
    } else {
        STALL_EP0();
    }

    CLEAR_EP0_CNT;
    SET_EP0_IN_RDY;
}

static void usb_set_interface_handler(struct usb_req_setup *req)
{
    usb_ep0_state = USB_EP0_STATE_DEFAULT;

    if (req->wValue == 0) {
        if (req->wIndex == 0) {
            CLEAR_EP0_CNT;
            SET_EP0_IN_RDY;
            SET_EP0_OUT_STALL;
        } else if (req->wIndex == 1) {
            CLEAR_EP0_CNT;
            SET_EP0_IN_RDY;
            SET_EP0_OUT_STALL;
        } else {
            STALL_EP0();
        }
    } else {
        STALL_EP0();
    }
}

static void usb_set_descriptor_handler()
{
    STALL_EP0();
}

static void usb_get_device_status_handler(struct usb_req_setup *req)
{
    usb_ep0_state = USB_EP0_STATE_RECV_STATUS;

    if ((req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_DEVICE) {
        if (usb_remote_wakeup) {
            EP0_IN_BUF[0] = 0x02;
        } else {
            EP0_IN_BUF[0] = 0x00;
        }

        EP0_IN_BUF[1] = 0;

        SET_EP0_CNT(2);
        SET_EP0_IN_RDY;
    } else {
        STALL_EP0();
    }
}

static void usb_get_interface_status_handler(struct usb_req_setup *req)
{
    if ((req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_IFACE) {
        if ((req->wIndex == 0) || (req->wIndex == 1)) {
            EP0_IN_BUF[0] = 0;
            EP0_IN_BUF[1] = 0;

            SET_EP0_CNT(2);
            SET_EP0_IN_RDY;
        } else {
            STALL_EP0();
        }
    } else {
        STALL_EP0();
    }
}

static void usb_get_endpoint_status_handler(struct usb_req_setup *req)
{
    if ((req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_ENDPT) {
        if (req->wIndex == 0x80) {
            EP0_IN_BUF[0] = ((EP0CON & 0x02)) >> 1;
            EP0_IN_BUF[1] = 0;
        } else if (req->wIndex == 0x81) {
            EP0_IN_BUF[0] = ((EP1CON & 0x08)) >> 3;
            EP0_IN_BUF[1] = 0;
        } else if (req->wIndex == 0x82) {
            EP0_IN_BUF[0] = ((EP2CON & 0x08)) >> 3;
            EP0_IN_BUF[1] = 0;
        } else {
            STALL_EP0();
            return;
        }

        SET_EP0_CNT(2);
        SET_EP0_IN_RDY;
    } else {
        STALL_EP0();
    }
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
            STALL_EP0();
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
            STALL_EP0();
            return;
        }
    } else {
        STALL_EP0();
        return;
    }

    uint16_t received_length = req->wLength;
    // truncate data to the allowed lengths received from the host
    length = (received_length < length) ? received_length : length;
    setup_ep0_in_xfer(addr, length);
    step_ep0_in_xfer();

    SET_EP0_IN_RDY;
}

static void usb_get_configuration_handler()
{
    usb_ep0_state = USB_EP0_STATE_RECV_STATUS;

    EP0_IN_BUF[0] = active_configuration;

    SET_EP0_CNT(1);
    SET_EP0_IN_RDY;
}

static void usb_get_interface_handler()
{
    usb_ep0_state = USB_EP0_STATE_RECV_STATUS;

    EP0_IN_BUF[0] = 0;

    SET_EP0_CNT(1);
    SET_EP0_IN_RDY;
}

static void usb_hid_get_report_handler()
{
    STALL_EP0();
}

static void usb_hid_set_report_handler(struct usb_req_setup *req)
{
    switch (req->wValue >> 8) {
        case REPORT_TYPE_OUTPUT:
            if ((req->wIndex == 0) && (req->wLength == 0x0001)) {
                usb_ep0_state = USB_EP0_STATE_LED;
                SET_EP0_OUT_RDY;
            }

            break;

        case REPORT_TYPE_FEATURE: {
            uint8_t report_id = (uint8_t)(req->wValue & 0xff);
            (void)report_id;
#ifdef ISP_ENABLE
            if (report_id == REPORT_ID_ISP) {
                usb_ep0_state = USB_EP0_STATE_ISP;
                SET_EP0_OUT_RDY;
            }
#endif
#if DEBUG == 1
            if (report_id == REPORT_ID_CONSOLE) {
                usb_ep0_state = USB_EP0_STATE_CONSOLE;
                SET_EP0_OUT_RDY;
            }
#endif

            break;
        }

        default:
            STALL_EP0();
            break;
    }
}

static void usb_hid_set_idle_handler(struct usb_req_setup *req)
{
    // TODO: finish implementaiton to use the set idle time
    idle_time = req->wValue >> 8;

    CLEAR_EP0_CNT;
    SET_EP0_IN_RDY;
}

static void usb_hid_get_idle_handler()
{
    EP0_IN_BUF[0] = idle_time;

    SET_EP0_CNT(1);
    SET_EP0_IN_RDY;
}

static void usb_hid_set_protocol_handler(struct usb_req_setup *req)
{
    if (req->wIndex == 0) {
        interface0_protocol = req->wValue & 0xff;
    } else if (req->wIndex == 1) {
        interface1_protocol = req->wValue & 0xff;
    }

    CLEAR_EP0_CNT;
    SET_EP0_IN_RDY;
}

static void usb_hid_get_protocol_handler(struct usb_req_setup *req)
{
    if (req->wIndex == 0) {
        EP0_IN_BUF[0] = interface0_protocol;
    } else if (req->wIndex == 1) {
        EP0_IN_BUF[0] = interface1_protocol;
    }

    SET_EP0_CNT(1);
    SET_EP0_IN_RDY;
}

void usb_ep0_out_irq()
{
    if (usb_ep0_state == USB_EP0_STATE_LED) {
        usb_ep0_state = 0;

        keyboard_set_led_state(EP0_OUT_BUF[0]);

        CLEAR_EP0_CNT;
        SET_EP0_IN_RDY;
#ifdef ISP_ENABLE
    } else if (usb_ep0_state == USB_EP0_STATE_ISP) {
        usb_ep0_state = 0;

        if (EP0_OUT_BUF[0] == 0x05 && EP0_OUT_BUF[1] == 0x75) {
            usb_isp_requested = 1;
        }
#endif
#if DEBUG == 1
    } else if (usb_ep0_state == USB_EP0_STATE_CONSOLE) {
        usb_ep0_state = 0;

        console_notify_attached(); // host tool is listening; start draining

        CLEAR_EP0_CNT;
        SET_EP0_IN_RDY;
#endif
    } else {
        usb_ep0_state = 0;
        CLEAR_EP0_CNT;
        SET_EP0_IN_RDY;
    }
}

void usb_ep0_in_irq()
{
    if (usb_ep0_state == USB_EP0_STATE_IN_DATA) {
        step_ep0_in_xfer();

        SET_EP0_IN_RDY;
        SET_EP0_OUT_RDY;
    } else if (usb_ep0_state == USB_EP0_STATE_RECV_STATUS) {
        SET_EP0_OUT_RDY;
        SET_EP0_IN_STALL;
    } else {
        SET_EP0_IN_STALL;
        SET_EP0_OUT_RDY;

        USBADDR = received_usb_addr;
    }
}

static void setup_ep0_in_xfer(uint8_t *src, uint16_t len)
{
    ep0_xfer_src        = src;
    ep0_xfer_bytes_left = len;
}

static void step_ep0_in_xfer()
{
    if (ep0_xfer_bytes_left == 0) {
        usb_ep0_state = USB_EP0_STATE_RECV_STATUS;

        SET_EP0_CNT(0);
    } else if (ep0_xfer_bytes_left > EP0_BUF_SIZE) {
        usb_ep0_state = USB_EP0_STATE_IN_DATA;
        set_ep0_in_buffer(ep0_xfer_src, EP0_BUF_SIZE);
        ep0_xfer_bytes_left -= EP0_BUF_SIZE;
        ep0_xfer_src += EP0_BUF_SIZE;

        SET_EP0_CNT(EP0_BUF_SIZE);
    } else if (ep0_xfer_bytes_left == EP0_BUF_SIZE) {
        usb_ep0_state = USB_EP0_STATE_IN_DATA;
        set_ep0_in_buffer(ep0_xfer_src, EP0_BUF_SIZE);
        ep0_xfer_bytes_left = 0;

        SET_EP0_CNT(EP0_BUF_SIZE);
    } else {
        usb_ep0_state = USB_EP0_STATE_RECV_STATUS;
        set_ep0_in_buffer(ep0_xfer_src, ep0_xfer_bytes_left);

        SET_EP0_CNT(ep0_xfer_bytes_left);
    }
}

static void set_ep0_in_buffer(uint8_t *src, uint8_t len)
{
    if (len > EP0_BUF_SIZE) {
        return;
    }
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
