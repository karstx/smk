#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef VIA_ENABLE

#    define RAW_HID_REPORT_SIZE 32u

void raw_hid_init(void);
void raw_hid_task(void);
void raw_hid_receive_irq(void);
void raw_hid_in_complete(void);

#endif
