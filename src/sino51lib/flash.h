#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FLASH_CODE,
    FLASH_DATA,
} flash_window_t;

void flash_read_into(flash_window_t window, uint16_t addr, __xdata uint8_t *dst, uint8_t len);
bool flash_matches(flash_window_t window, uint16_t addr, const __xdata uint8_t *src, uint8_t len);

void flash_program_byte(flash_window_t window, uint16_t addr, uint8_t value);

void flash_program_from(flash_window_t window, uint16_t addr, const __xdata uint8_t *src, uint8_t len);

void flash_erase(flash_window_t window, uint16_t addr);
