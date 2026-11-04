#include "via.h"

#ifdef VIA_ENABLE

#    include "dynamic_keymap.h"
#    include "kbdef.h"
#    include "raw_hid.h"
#    include "settings.h"
#    include <string.h>

#    define VIA_ID_GET_PROTOCOL_VERSION           0x01u
#    define VIA_ID_GET_KEYBOARD_VALUE             0x02u
#    define VIA_ID_SET_KEYBOARD_VALUE             0x03u
#    define VIA_ID_DYNAMIC_KEYMAP_GET_KEYCODE     0x04u
#    define VIA_ID_DYNAMIC_KEYMAP_SET_KEYCODE     0x05u
#    define VIA_ID_DYNAMIC_KEYMAP_RESET           0x06u
#    define VIA_ID_CUSTOM_SET_VALUE               0x07u
#    define VIA_ID_CUSTOM_GET_VALUE               0x08u
#    define VIA_ID_CUSTOM_SAVE                    0x09u
#    define VIA_ID_EEPROM_RESET                   0x0Au
#    define VIA_ID_BOOTLOADER_JUMP                0x0Bu
#    define VIA_ID_DYNAMIC_KEYMAP_MACRO_GET_COUNT 0x0Cu
#    define VIA_ID_DYNAMIC_KEYMAP_MACRO_GET_BUFFER_SIZE 0x0Du
#    define VIA_ID_DYNAMIC_KEYMAP_MACRO_GET_BUFFER 0x0Eu
#    define VIA_ID_DYNAMIC_KEYMAP_MACRO_SET_BUFFER 0x0Fu
#    define VIA_ID_DYNAMIC_KEYMAP_MACRO_RESET     0x10u
#    define VIA_ID_DYNAMIC_KEYMAP_GET_LAYER_COUNT 0x11u
#    define VIA_ID_DYNAMIC_KEYMAP_GET_BUFFER      0x12u
#    define VIA_ID_DYNAMIC_KEYMAP_SET_BUFFER      0x13u
#    define VIA_ID_UNHANDLED                      0xFFu

#    define VIA_KEYBOARD_VALUE_UPTIME             0x01u
#    define VIA_KEYBOARD_VALUE_LAYOUT_OPTIONS     0x02u
#    define VIA_KEYBOARD_VALUE_SWITCH_MATRIX_STATE 0x03u
#    define VIA_KEYBOARD_VALUE_FIRMWARE_VERSION   0x04u
#    define VIA_KEYBOARD_VALUE_DEVICE_INDICATION  0x05u
#    define VIA_KEYBOARD_VALUE_KEYCODES_VERSION   0x06u

void via_process_packet(__xdata uint8_t *data)
{
    switch (data[0]) {
        case VIA_ID_GET_PROTOCOL_VERSION:
            data[1] = (uint8_t)(VIA_PROTOCOL_VERSION >> 8);
            data[2] = (uint8_t)VIA_PROTOCOL_VERSION;
            break;

        case VIA_ID_GET_KEYBOARD_VALUE:
            switch (data[1]) {
                case VIA_KEYBOARD_VALUE_LAYOUT_OPTIONS:
                case VIA_KEYBOARD_VALUE_FIRMWARE_VERSION:
                    memset(&data[2], 0, 4);
                    break;
                case VIA_KEYBOARD_VALUE_SWITCH_MATRIX_STATE:
                    memset(&data[2], 0, RAW_HID_REPORT_SIZE - 2u);
                    break;
                case VIA_KEYBOARD_VALUE_KEYCODES_VERSION:
                    // QMK keycodes.h version 0.0.9, encoded as a 32-bit BCD value.
                    data[2] = 0x00;
                    data[3] = 0x00;
                    data[4] = 0x00;
                    data[5] = 0x09;
                    break;
                case VIA_KEYBOARD_VALUE_UPTIME:
                case VIA_KEYBOARD_VALUE_DEVICE_INDICATION:
                default:
                    data[0] = VIA_ID_UNHANDLED;
                    break;
            }
            break;

        case VIA_ID_SET_KEYBOARD_VALUE:
            if (data[1] != VIA_KEYBOARD_VALUE_LAYOUT_OPTIONS && data[1] != VIA_KEYBOARD_VALUE_DEVICE_INDICATION) {
                data[0] = VIA_ID_UNHANDLED;
            }
            break;

        case VIA_ID_DYNAMIC_KEYMAP_GET_KEYCODE: {
            uint16_t keycode = keymap_get_keycode(data[1], data[2], data[3]);
            data[4] = (uint8_t)(keycode >> 8);
            data[5] = (uint8_t)keycode;
            break;
        }

        case VIA_ID_DYNAMIC_KEYMAP_SET_KEYCODE: {
            uint16_t keycode = (uint16_t)((uint16_t)data[4] << 8) | data[5];
            if (!dynamic_keymap_set_keycode(data[1], data[2], data[3], keycode)) {
                data[0] = VIA_ID_UNHANDLED;
            }
            break;
        }

        case VIA_ID_DYNAMIC_KEYMAP_RESET:
            dynamic_keymap_reset();
            break;

        case VIA_ID_DYNAMIC_KEYMAP_GET_LAYER_COUNT:
            data[1] = dynamic_keymap_layer_count();
            break;

        case VIA_ID_DYNAMIC_KEYMAP_GET_BUFFER: {
            uint16_t offset    = (uint16_t)((uint16_t)data[1] << 8) | data[2];
            uint8_t  requested = data[3];
            uint8_t  size      = requested;
            if (size > RAW_HID_REPORT_SIZE - 4u) {
                size = RAW_HID_REPORT_SIZE - 4u;
            }
            dynamic_keymap_get_buffer(offset, size, &data[4]);
            data[3] = requested;
            break;
        }

        case VIA_ID_DYNAMIC_KEYMAP_SET_BUFFER: {
            uint16_t offset    = (uint16_t)((uint16_t)data[1] << 8) | data[2];
            uint8_t  requested = data[3];
            uint8_t  size      = requested;
            if (size > RAW_HID_REPORT_SIZE - 4u) {
                size = RAW_HID_REPORT_SIZE - 4u;
            }
            dynamic_keymap_set_buffer(offset, size, &data[4]);
            data[3] = requested;
            break;
        }

        case VIA_ID_DYNAMIC_KEYMAP_MACRO_GET_COUNT:
            data[1] = 0;
            break;

        case VIA_ID_DYNAMIC_KEYMAP_MACRO_GET_BUFFER_SIZE:
            data[1] = 0;
            data[2] = 0;
            break;

        case VIA_ID_DYNAMIC_KEYMAP_MACRO_GET_BUFFER:
        case VIA_ID_DYNAMIC_KEYMAP_MACRO_SET_BUFFER:
        case VIA_ID_DYNAMIC_KEYMAP_MACRO_RESET:
            data[0] = VIA_ID_UNHANDLED;
            break;

        case VIA_ID_CUSTOM_GET_VALUE:
        case VIA_ID_CUSTOM_SET_VALUE:
        case VIA_ID_CUSTOM_SAVE:
        case VIA_ID_EEPROM_RESET:
        case VIA_ID_BOOTLOADER_JUMP:
        default:
            data[0] = VIA_ID_UNHANDLED;
            break;
    }
}

#endif
