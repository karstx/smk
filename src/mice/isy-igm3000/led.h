#pragma once
#include <stdint.h>

#define LED_MODE_RAINBOW 0 // default
#define LED_MODE_BREATHE 1
#define LED_MODE_STATIC  2
#define LED_MODE_OFF     3
#define LED_MODE_COUNT   4

extern uint8_t led_mode;
extern uint8_t led_override; // 1 = diag override active
extern uint8_t led_dark;     // 1 = forced off (USB suspend)

void led_init(void);
void led_task_10ms(void);
void led_dpi_indicator(uint8_t level); // 4 DPI LEDs on P4.0..P4.3
void led_set_override(uint8_t r, uint8_t g, uint8_t b);
void led_clear_override(void);
void led_next_mode(void);
