#pragma once

#include "kbdef.h"
#include "layout.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef DYNAMIC_KEYMAP_ENABLE

#ifndef DYNAMIC_KEYMAP_LAYER_COUNT
#    define DYNAMIC_KEYMAP_LAYER_COUNT 4u
#endif

#define DYNAMIC_KEYMAP_BYTES ((uint16_t)DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2u)

void dynamic_keymap_init(void);
void dynamic_keymap_tick(void);
void dynamic_keymap_task(void);

uint8_t  dynamic_keymap_layer_count(void);
uint16_t keymap_get_keycode(uint8_t layer, uint8_t row, uint8_t col);
bool     dynamic_keymap_set_keycode(uint8_t layer, uint8_t row, uint8_t col, uint16_t keycode);

uint8_t dynamic_keymap_get_buffer(uint16_t offset, uint8_t size, __xdata uint8_t *data);
uint8_t dynamic_keymap_set_buffer(uint16_t offset, uint8_t size, const __xdata uint8_t *data);

void dynamic_keymap_reset(void);

#else

inline uint16_t keymap_get_keycode(uint8_t layer, uint8_t row, uint8_t col)
{
    return keymaps[layer][row][col];
}

#endif
