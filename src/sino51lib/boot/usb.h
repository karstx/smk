#pragma once

#include <stdbool.h>
#include <stdint.h>

void boot_usb_start(void);
void boot_usb_stop(void);
void boot_usb_poll(void);

/*
 * Feature-report plumbing, implemented by isp.c. A page report is far larger
 * than the 8-byte control buffer, so it is streamed a byte at a time rather
 * than staged in RAM: `index` counts from the report ID at 0.
 */
bool    isp_get_report_begin(uint8_t report_id, uint16_t *length);
uint8_t isp_get_report_byte(uint16_t index);
void    isp_get_report_end(void);

bool isp_set_report_begin(uint8_t report_id, uint16_t length);
void isp_set_report_byte(uint16_t index, uint8_t value);
void isp_set_report_end(void);
