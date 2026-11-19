// Single RGB zone on hardware PWM0/1/2 (stock 0x0AEB / 0x0C9A): PWM0 = red,
// PWM1 = green, PWM2 = blue, higher duty = brighter (verified on the device).
// Four DPI indicator LEDs on P4.0..P4.3, active low (stock 0x403A).
#include "led.h"
#include "board.h"

#ifndef LED_INVERT
#    define LED_INVERT 0
#endif

uint8_t led_mode = LED_MODE_RAINBOW;
uint8_t led_override;
uint8_t led_dark;

static uint16_t hue;     // 0..1535
static uint8_t  breathe; // 0..255 triangle
static __bit    breathe_down;
static uint8_t  ov_r, ov_g, ov_b;
static uint8_t  dark_applied;

static uint16_t to_duty(uint8_t v)
{
    // rough gamma: v^2 / 16 -> 0..4064 (12-bit period 0x0FFF)
    uint16_t d = ((uint16_t)v * v) >> 4;
#if LED_INVERT
    d = 0x0fff - d;
#endif
    return d;
}

static void pwm_write(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t dr = to_duty(r), dg = to_duty(g), db = to_duty(b);
    sfr_page_0();
    PWM0DL = (uint8_t)dr;
    PWM0DH = (uint8_t)(dr >> 8);
    PWM1DL = (uint8_t)dg;
    PWM1DH = (uint8_t)(dg >> 8);
    PWM2DL = (uint8_t)db;
    PWM2DH = (uint8_t)(db >> 8);
}

void led_init(void)
{
    sfr_page_0();
    PWM0PL = 0xff;
    PWM0PH = 0x0f;
    PWM1PL = 0xff;
    PWM1PH = 0x0f;
    PWM2PL = 0xff;
    PWM2PH = 0x0f;
    pwm_write(0, 0, 0);
    PWM0CON = 0xc1;
    PWM1CON = 0xc1;
    PWM2CON = 0xc1;

    // DPI indicator LEDs: P4.0..P4.3, active low (stock 0x403A). Latch high first.
    P4 |= 0x0f;
    P4CR |= 0x0f;
}

// stock table 0x4109, indexed by DPI level 1..6
static const __code uint8_t dpi_led_mask[6] = {0x01, 0x03, 0x07, 0x0c, 0x0e, 0x0f};
static uint8_t              dpi_led_level;

static void dpi_leds_apply(uint8_t on)
{
    sfr_page_0(); // P4 shares its address with EP1CON on SFR page 1
    uint8_t mask = on ? dpi_led_mask[dpi_led_level] : 0;
    P4           = (uint8_t)((P4 | 0x0f) & ~mask);
}

void led_dpi_indicator(uint8_t level)
{
    dpi_led_level = level < 6 ? level : 5;
    dpi_leds_apply(!led_dark);
}

static void hue_rgb(uint16_t h, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t seg = (uint8_t)(h >> 8);
    uint8_t f   = (uint8_t)h;
    switch (seg) {
        case 0:
            *r = 255;
            *g = f;
            *b = 0;
            break;
        case 1:
            *r = 255 - f;
            *g = 255;
            *b = 0;
            break;
        case 2:
            *r = 0;
            *g = 255;
            *b = f;
            break;
        case 3:
            *r = 0;
            *g = 255 - f;
            *b = 255;
            break;
        case 4:
            *r = f;
            *g = 0;
            *b = 255;
            break;
        default:
            *r = 255;
            *g = 0;
            *b = 255 - f;
            break;
    }
}

static uint8_t scale(uint8_t v, uint8_t k)
{
    return (uint8_t)(((uint16_t)v * k) >> 8);
}

void led_task_10ms(void)
{
    uint8_t r, g, b;

    hue += 2; // full cycle = 768 steps * 10 ms ~ 7.7 s
    if (hue >= 1536) {
        hue -= 1536;
    }
    if (breathe_down) {
        if (breathe <= 2)
            breathe_down = 0;
        else
            breathe -= 2;
    } else {
        if (breathe >= 253)
            breathe_down = 1;
        else
            breathe += 2;
    } // one breath ~ 2 x 128 x 10 ms ~ 2.6 s

    if (led_dark != dark_applied) { // USB suspend: everything off, restore on resume
        dark_applied = led_dark;
        dpi_leds_apply(!led_dark);
    }
    if (led_dark) {
        pwm_write(0, 0, 0);
        return;
    }
    if (led_override) {
        pwm_write(ov_r, ov_g, ov_b);
        return;
    }
    switch (led_mode) {
        case LED_MODE_RAINBOW:
            hue_rgb(hue, &r, &g, &b);
            break;
        case LED_MODE_BREATHE:
            hue_rgb(hue, &r, &g, &b);
            r = scale(r, breathe);
            g = scale(g, breathe);
            b = scale(b, breathe);
            break;
        case LED_MODE_STATIC:
            hue_rgb(1024, &r, &g, &b); // blue-violet
            break;
        default:
            r = g = b = 0;
            break;
    }
    pwm_write(r, g, b);
}

void led_set_override(uint8_t r, uint8_t g, uint8_t b)
{
    ov_r         = r;
    ov_g         = g;
    ov_b         = b;
    led_override = 1;
}

void led_clear_override(void)
{
    led_override = 0;
}

void led_next_mode(void)
{
    led_mode++;
    if (led_mode >= LED_MODE_COUNT) {
        led_mode = 0;
    }
}
