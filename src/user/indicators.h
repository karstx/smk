#pragma once

#include "keyboard.h"
#include <stdint.h>
#include <stdbool.h>

void indicators_init();
void indicators_start();
void indicators_render();
void indicators_pre_update();
bool indicators_update_step(keyboard_state_t *keyboard, uint8_t current_step);
void indicators_post_update();
// Blank the LEDs around a matrix scan: the LED drive couples into the row sense
// and biases the sample, so it's silenced for the sweep and restored afterward.
void indicators_pwm_enable();
void indicators_pwm_disable();
void indicators_apply_defaults();
// every debounced key change, by matrix position (for key-reactive effects)
void indicators_key_event(uint8_t row, uint8_t col, bool pressed);
void indicators_validate_settings();
