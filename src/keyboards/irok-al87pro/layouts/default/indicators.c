#include "indicators.h"
#include "kbdef.h"
#include "gpio.h"
#include "pwm.h"
#include "settings.h"
#include "tick.h"
#include "led_effect.h"
#include "user_led.h"
#include "user_matrix.h"
#include "delay.h"
#include "byk916_bk3632_link.h"
#ifdef PER_KEY_RGB_ENABLE
#    include "per_key_rgb.h"
#endif
#include <stdbool.h>
#include <stdint.h>

#ifndef FX_REACTIVE
#    define FX_REACTIVE 4
#endif

#define LED_ROWS MATRIX_ROWS
#define LED_COLS MATRIX_COLS

#define PWM_PERIOD 0x0100u
// The BYK916 VDR/NPN path is active-high at the LED current sink. On this
// board a larger duty value produces more light; keep black electrically off.
#define LED_DUTY(v) (uint16_t)(uint8_t)(v)
#ifdef BYK_RGB_IRQ_TRIM
/* This file only loads 8-bit RGB duties into 16-bit PWM registers.
 * Keep every high-byte write and H-before-L ordering, but evaluate/read the
 * framebuffer once. No change to period, polarity, pin mux or brightness. */
#undef SET_PWM_DUTY_2
#define SET_PWM_DUTY_2(pwm, value) do { \
    uint8_t duty8 = (uint8_t)(value); \
    PWM_DUTY_REG(pwm, 2, H) = 0; \
    PWM_DUTY_REG(pwm, 2, L) = duty8; \
} while (0)
#endif

#define LED_BRIGHTNESS_DEFAULT 48u
#define LED_BRIGHTNESS_STEP    16u
#define LED_SPEED_DEFAULT      2u
#define LED_SPEED_MIN          1u
#define LED_SPEED_MAX          16u
#define LED_PHASE_FRAMES       8u

#include LED_GEOMETRY_HEADER
_Static_assert(LED_GEOMETRY_ROWS == LED_ROWS && LED_GEOMETRY_COLS == LED_COLS, "generated LED geometry size does not match the key matrix");

static __xdata uint8_t led_fb[LED_ROWS][3][LED_COLS];
static uint8_t         led_col;
static uint8_t         led_phase;
static uint8_t         phase_frames;
static volatile bool   render_dirty;
static __xdata uint8_t reactive_level[LED_ROWS][LED_COLS];

extern bool kb_gui_locked(void);

static void reactive_clear(void)
{
    for (uint8_t row = 0; row < LED_ROWS; row++) {
        for (uint8_t col = 0; col < LED_COLS; col++) reactive_level[row][col] = 0;
    }
}

static bool led_position_present(uint8_t row, uint8_t col)
{
    if (row == 0) return col != 1;
    if (row == 1 || row == 2) return true;
    if (row == 3) return col != 12 && col < 14;
    if (row == 4) return col <= 10 || col == 13 || col == 15;
    return col <= 2 || col == 5 || (col >= 8 && col <= 10) || col >= 13;
}

static void led_set_charge_duties(void)
{
    SET_PWM_DUTY_2(LED_PWM_R0, LED_DUTY(48u));
    SET_PWM_DUTY_2(LED_PWM_G0, LED_DUTY(12u));
    SET_PWM_DUTY_2(LED_PWM_B0, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_R1, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_G1, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_B1, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_R2, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_G2, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_B2, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_R3, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_G3, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_B3, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_R4, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_G4, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_B4, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_R5, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_G5, LED_DUTY(0));
    SET_PWM_DUTY_2(LED_PWM_B5, LED_DUTY(0));
}

static void led_set_vdr_duties(uint8_t col)
{
    SET_PWM_DUTY_2(LED_PWM_R0, LED_DUTY(led_fb[0][0][col]));
    SET_PWM_DUTY_2(LED_PWM_G0, LED_DUTY(led_fb[0][1][col]));
    SET_PWM_DUTY_2(LED_PWM_B0, LED_DUTY(led_fb[0][2][col]));
    SET_PWM_DUTY_2(LED_PWM_R1, LED_DUTY(led_fb[1][0][col]));
    SET_PWM_DUTY_2(LED_PWM_G1, LED_DUTY(led_fb[1][1][col]));
    SET_PWM_DUTY_2(LED_PWM_B1, LED_DUTY(led_fb[1][2][col]));
    SET_PWM_DUTY_2(LED_PWM_R2, LED_DUTY(led_fb[2][0][col]));
    SET_PWM_DUTY_2(LED_PWM_G2, LED_DUTY(led_fb[2][1][col]));
    SET_PWM_DUTY_2(LED_PWM_B2, LED_DUTY(led_fb[2][2][col]));
    SET_PWM_DUTY_2(LED_PWM_R3, LED_DUTY(led_fb[3][0][col]));
    SET_PWM_DUTY_2(LED_PWM_G3, LED_DUTY(led_fb[3][1][col]));
    SET_PWM_DUTY_2(LED_PWM_B3, LED_DUTY(led_fb[3][2][col]));
    if (col == LED_INDICATOR_COL) {
        // These three matrix junctions are physically shared with the discrete
        // Caps/Win/Scroll LEDs. Never feed them animation data: otherwise the
        // indicators follow the RGB effect and appear to light together.
        SET_PWM_DUTY_2(LED_PWM_R4, LED_DUTY(0));
        SET_PWM_DUTY_2(LED_PWM_G4, LED_DUTY(0));
        SET_PWM_DUTY_2(LED_PWM_B4, LED_DUTY(0));
    } else {
        SET_PWM_DUTY_2(LED_PWM_R4, LED_DUTY(led_fb[4][0][col]));
        SET_PWM_DUTY_2(LED_PWM_G4, LED_DUTY(led_fb[4][1][col]));
        SET_PWM_DUTY_2(LED_PWM_B4, LED_DUTY(led_fb[4][2][col]));
    }
    SET_PWM_DUTY_2(LED_PWM_R5, LED_DUTY(led_fb[5][0][col]));
    SET_PWM_DUTY_2(LED_PWM_G5, LED_DUTY(led_fb[5][1][col]));
    SET_PWM_DUTY_2(LED_PWM_B5, LED_DUTY(led_fb[5][2][col]));
}

void indicators_apply_defaults(void)
{
    user_settings.led_effect     = FX_RADIAL;
    user_settings.led_brightness = LED_BRIGHTNESS_DEFAULT;
    user_settings.led_speed      = LED_SPEED_DEFAULT;
    user_settings.rf_link        = 1u;
}

void indicators_validate_settings(void)
{
    bool corrected = false;
    if (user_settings.led_effect > FX_OFF) {
        user_settings.led_effect = FX_RADIAL;
        corrected = true;
    }
    if (user_settings.led_speed < LED_SPEED_MIN || user_settings.led_speed > LED_SPEED_MAX) {
        user_settings.led_speed = LED_SPEED_DEFAULT;
        corrected = true;
    }
    if (user_settings.rf_link < 1u || user_settings.rf_link > 4u) {
        user_settings.rf_link = 1u;
        corrected = true;
    }
    if (corrected) settings_mark_dirty();
}

void indicators_init(void)
{
    led_col       = 0;
    led_phase     = 0;
    phase_frames  = 0;
    render_dirty  = true;
    reactive_clear();
    indicators_pwm_disable();
}

void indicators_start(void)
{
    render_dirty = true;
}

void indicators_render_request(void)
{
    render_dirty = true;
}

void indicators_key_event(uint8_t row, uint8_t col, bool pressed)
{
    if (user_settings.led_effect == FX_REACTIVE && row < LED_ROWS && col < LED_COLS && pressed) {
        reactive_level[row][col] = 255u;
        render_dirty = true;
    }
}

#ifdef BYK_RGB_RENDER_SLICE
/* Foreground only. Electrical LED multiplex remains ISR-owned and unchanged.
 * A pass is progressive (like original ISR-visible framebuffer updates), not
 * a second full buffer. Latch animation phase, never restart an incomplete
 * pass when another dirty request arrives: finish it then consume the request. */
static uint8_t render_row,render_col,render_phase;
static bool render_active;
void indicators_render(void)
{
    uint8_t budget=6;
    if(!render_active) {
        bool start;
        __critical {
            start=render_dirty;
            if(start){render_dirty=false;render_phase=led_phase;}
        }
        if(!start)return;
        render_row=render_col=0;render_active=true;
    }
    while(budget--) {
        uint8_t row=render_row,col=render_col;
            uint8_t rgb[3] = {0, 0, 0};
#ifdef BYK_HOTPATH_TRIM
            bool asleep=byk916_link_rgb_asleep();
            bool custom=false;
#ifdef PER_KEY_RGB_ENABLE
            /* Successful override writes ALL RGB channels; skip the base
             * colour calculation that would otherwise be overwritten. */
#ifdef BYK_RGB_OFF_GUARD
            if(!asleep && user_settings.led_effect<FX_OFF)custom=per_key_rgb_apply(row,col,user_settings.led_brightness,rgb);
#else
            if(!asleep)custom=per_key_rgb_apply(row,col,user_settings.led_brightness,rgb);
#endif
#endif
#endif
            if (
#ifdef BYK_HOTPATH_TRIM
                !custom && !asleep && led_position_present(row,col) && user_settings.led_effect<FX_OFF
#else
                led_position_present(row, col) && user_settings.led_effect < FX_OFF && !byk916_link_rgb_asleep()
#endif
            ) {
                if (user_settings.led_effect == FX_REACTIVE) {
                    uint8_t level = (uint8_t)(((uint16_t)reactive_level[row][col] * user_settings.led_brightness) >> 8);
                    led_color_wheel((uint8_t)(user_led_radial(row, col) + render_phase), rgb);
                    rgb[0] = (uint8_t)(((uint16_t)rgb[0] * level) >> 8);
                    rgb[1] = (uint8_t)(((uint16_t)rgb[1] * level) >> 8);
                    rgb[2] = (uint8_t)(((uint16_t)rgb[2] * level) >> 8);
                } else {
                    led_effect_rgb((led_effect_t)user_settings.led_effect, row, col, render_phase, user_settings.led_brightness, rgb);
                }
            }
#if defined(PER_KEY_RGB_ENABLE) && !defined(BYK_HOTPATH_TRIM)
#ifdef BYK_RGB_OFF_GUARD
            if (user_settings.led_effect<FX_OFF && !byk916_link_rgb_asleep()) per_key_rgb_apply(row, col, user_settings.led_brightness, rgb);
#else
            if (!byk916_link_rgb_asleep()) per_key_rgb_apply(row, col, user_settings.led_brightness, rgb);
#endif
#endif
            byk916_bk3632_slot_indicator(row, col, rgb);
            if (byk916_bk3632_charging() && row == 0u && col == 16u) {
                rgb[0] = 48u;
                rgb[1] = 12u;
                rgb[2] = 0u;
            }
            led_fb[row][0][col] = rgb[0];
            led_fb[row][1][col] = rgb[1];
            led_fb[row][2][col] = rgb[2];
        if(++render_col>=LED_COLS) {
            render_col=0;
            if(++render_row>=LED_ROWS){render_active=false;break;}
        }
    }
}
#else
void indicators_render(void)
{
    if (!render_dirty) {
        return;
    }
    render_dirty = false;

    for (uint8_t row = 0; row < LED_ROWS; row++) {
        for (uint8_t col = 0; col < LED_COLS; col++) {
            uint8_t rgb[3] = {0, 0, 0};
            if (led_position_present(row, col) && user_settings.led_effect < FX_OFF && !byk916_link_rgb_asleep()) {
                if (user_settings.led_effect == FX_REACTIVE) {
                    uint8_t level = (uint8_t)(((uint16_t)reactive_level[row][col] * user_settings.led_brightness) >> 8);
                    led_color_wheel((uint8_t)(user_led_radial(row, col) + led_phase), rgb);
                    rgb[0] = (uint8_t)(((uint16_t)rgb[0] * level) >> 8);
                    rgb[1] = (uint8_t)(((uint16_t)rgb[1] * level) >> 8);
                    rgb[2] = (uint8_t)(((uint16_t)rgb[2] * level) >> 8);
                } else {
                    led_effect_rgb((led_effect_t)user_settings.led_effect, row, col, led_phase, user_settings.led_brightness, rgb);
                }
            }
#ifdef PER_KEY_RGB_ENABLE
#ifdef BYK_RGB_OFF_GUARD
            if (user_settings.led_effect<FX_OFF && !byk916_link_rgb_asleep()) per_key_rgb_apply(row, col, user_settings.led_brightness, rgb);
#else
            if (!byk916_link_rgb_asleep()) per_key_rgb_apply(row, col, user_settings.led_brightness, rgb);
#endif
#endif
            byk916_bk3632_slot_indicator(row, col, rgb);
            if (byk916_bk3632_charging() && row == 0u && col == 16u) {
                rgb[0] = 48u;
                rgb[1] = 12u;
                rgb[2] = 0u;
            }
            led_fb[row][0][col] = rgb[0];
            led_fb[row][1][col] = rgb[1];
            led_fb[row][2][col] = rgb[2];
        }
    }
}
#endif

#ifdef BYK_RGB_OFF_GUARD
/* ISR-only OFF/dim path: never read progressively rendered RGB/custom data.
 * Start with black and synthesize only the live slot/charge overlays. This
 * also closes the window before the foreground renderer clears old colours. */
static bool led_set_status_only_duties(uint8_t col)
{
    uint8_t rgb[3];
    bool any=false;
    rgb[0]=rgb[1]=rgb[2]=0;
    (void)byk916_bk3632_slot_indicator(0u,col,rgb);
    if(byk916_bk3632_charging() && col==16u){rgb[0]=48;rgb[1]=12;rgb[2]=0;}
    if(rgb[0]||rgb[1]||rgb[2])any=true;
    SET_PWM_DUTY_2(LED_PWM_R0,LED_DUTY(rgb[0]));
    SET_PWM_DUTY_2(LED_PWM_G0,LED_DUTY(rgb[1]));
    SET_PWM_DUTY_2(LED_PWM_B0,LED_DUTY(rgb[2]));
    rgb[0]=rgb[1]=rgb[2]=0;
    (void)byk916_bk3632_slot_indicator(1u,col,rgb);
    if(rgb[0]||rgb[1]||rgb[2])any=true;
    SET_PWM_DUTY_2(LED_PWM_R1,LED_DUTY(rgb[0]));
    SET_PWM_DUTY_2(LED_PWM_G1,LED_DUTY(rgb[1]));
    SET_PWM_DUTY_2(LED_PWM_B1,LED_DUTY(rgb[2]));
    rgb[0]=rgb[1]=rgb[2]=0;
    (void)byk916_bk3632_slot_indicator(2u,col,rgb);
    if(rgb[0]||rgb[1]||rgb[2])any=true;
    SET_PWM_DUTY_2(LED_PWM_R2,LED_DUTY(rgb[0]));
    SET_PWM_DUTY_2(LED_PWM_G2,LED_DUTY(rgb[1]));
    SET_PWM_DUTY_2(LED_PWM_B2,LED_DUTY(rgb[2]));
    rgb[0]=rgb[1]=rgb[2]=0;
    (void)byk916_bk3632_slot_indicator(3u,col,rgb);
    if(rgb[0]||rgb[1]||rgb[2])any=true;
    SET_PWM_DUTY_2(LED_PWM_R3,LED_DUTY(rgb[0]));
    SET_PWM_DUTY_2(LED_PWM_G3,LED_DUTY(rgb[1]));
    SET_PWM_DUTY_2(LED_PWM_B3,LED_DUTY(rgb[2]));
    rgb[0]=rgb[1]=rgb[2]=0;
    (void)byk916_bk3632_slot_indicator(4u,col,rgb);
    if(col==LED_INDICATOR_COL)rgb[0]=rgb[1]=rgb[2]=0;
    if(rgb[0]||rgb[1]||rgb[2])any=true;
    SET_PWM_DUTY_2(LED_PWM_R4,LED_DUTY(rgb[0]));
    SET_PWM_DUTY_2(LED_PWM_G4,LED_DUTY(rgb[1]));
    SET_PWM_DUTY_2(LED_PWM_B4,LED_DUTY(rgb[2]));
    rgb[0]=rgb[1]=rgb[2]=0;
    (void)byk916_bk3632_slot_indicator(5u,col,rgb);
    if(rgb[0]||rgb[1]||rgb[2])any=true;
    SET_PWM_DUTY_2(LED_PWM_R5,LED_DUTY(rgb[0]));
    SET_PWM_DUTY_2(LED_PWM_G5,LED_DUTY(rgb[1]));
    SET_PWM_DUTY_2(LED_PWM_B5,LED_DUTY(rgb[2]));
    return any;
}
#endif

static uint8_t led_indicator_gpio(keyboard_state_t *keyboard, uint8_t slot)
{
    if (slot == 0 && (keyboard->led_state & LED_INDICATOR_CAPS)) {
        return VDR4_R_P3_3;
    }
    if (slot == 1 && kb_gui_locked()) {
        return VDR4_G_P3_4;
    }
    if (slot == 2 && (keyboard->led_state & LED_INDICATOR_SCROLL)) {
        return VDR4_B_P3_5;
    }
    return 0;
}

void indicators_pre_update(void)
{
    indicators_pwm_disable();
}

bool indicators_update_step(keyboard_state_t *keyboard, uint8_t current_step)
{
    static bool slot_overlay_was_active;
    (void)current_step;

    if (byk916_bk3632_charge_standby()) {
        if (byk916_bk3632_charging() && led_col == 16u) {
            led_set_charge_duties();
            user_matrix_col_select(16u);
            delay_us(2);
            indicators_pwm_enable();
        }
        if (++led_col >= (LED_COLS + LED_INDICATOR_SUBFRAMES)) led_col = 0;
        return led_col == 0;
    }

    if (led_col < LED_COLS) {
        if ((!byk916_link_rgb_asleep() && user_settings.led_effect < FX_OFF) || byk916_bk3632_slot_indicator_active() || byk916_bk3632_charging()) {
#ifdef BYK_RGB_OFF_GUARD
            if(user_settings.led_effect>=FX_OFF || byk916_link_rgb_asleep()) {
                if(!led_set_status_only_duties(led_col))goto led_column_done;
            } else
#endif
            led_set_vdr_duties(led_col);
            user_matrix_col_select(led_col);
            // Allow the C-column PMOS gate and LED junction capacitance to settle
            // before enabling PWM, especially on the P6.0/P4.6 edge columns.
            delay_us(2);
            indicators_pwm_enable();
            if (led_col == LED_INDICATOR_COL) {
                // Duty=period is not a guaranteed electrical OFF state on this
                // PWM block. Hard-disable all three shared indicator channels.
                PWM03CON = PWM_CON_OFF;
                PWM04CON = PWM_CON_OFF;
                PWM05CON = PWM_CON_OFF;
            }
        }
    } else {
        uint8_t pin = led_indicator_gpio(keyboard, (uint8_t)(led_col - LED_COLS));
        if (pin) {
            // Keep every PWM group hard-off. Select the shared C15 PMOS supply
            // and drive exactly one NPN base as a plain GPIO for this subframe.
            user_matrix_col_select(LED_INDICATOR_COL);
            GPIO_HIGH(3, pin);
        }
    }

#ifdef BYK_RGB_OFF_GUARD
led_column_done:
    ;
#endif
    bool wrapped = false;
    if (++led_col >= (LED_COLS + LED_INDICATOR_SUBFRAMES)) {
        led_col = 0;
        wrapped = true;
        if (user_settings.led_effect == FX_REACTIVE) {
            bool active = false;
#ifdef FAST_LED_SCAN
            uint8_t decay = (uint8_t)(2u + ((user_settings.led_speed + 1u) >> 1));
#else
            uint8_t decay = (uint8_t)(4u + user_settings.led_speed);
#endif
            for (uint8_t row = 0; row < LED_ROWS; row++) {
                for (uint8_t col = 0; col < LED_COLS; col++) {
                    uint8_t v = reactive_level[row][col];
                    if (v) {
                        reactive_level[row][col] = (v > decay) ? (uint8_t)(v - decay) : 0;
                        active = true;
                    }
                }
            }
            if (active) render_dirty = true;
        }
        {
            bool slot_active = byk916_bk3632_slot_indicator_active();
            if (slot_active || slot_overlay_was_active || byk916_bk3632_charging()) render_dirty = true;
            slot_overlay_was_active = slot_active;
        }
#ifdef BYK_DYNAMIC_SCAN
        if(!byk916_link_rgb_asleep() && ++phase_frames >= LED_PHASE_FRAMES) {
#else
        if (++phase_frames >= LED_PHASE_FRAMES) {
#endif
            phase_frames = 0;
            led_phase = (uint8_t)(led_phase + user_settings.led_speed);
            render_dirty = true;
        }
    }
    return wrapped;
}

void indicators_post_update(void)
{
    // Clear PWM0's synchronized-start latch after the channels have started.
    PWM00CON &= (uint8_t)~(1u << 5);
}

void indicators_pwm_enable(void)
{
    PWM00CON = (uint8_t)(PWM_MODE_ENABLE | PWM_SS | PWM_CLK_DIV_4);
    PWM01CON = PWM_SS;
    PWM02CON = PWM_SS;
    PWM03CON = PWM_SS;
    PWM04CON = PWM_SS;
    PWM05CON = PWM_SS;

    PWM10CON = (uint8_t)(PWM_MODE_ENABLE | PWM_SS | PWM_CLK_DIV_4);
    PWM11CON = PWM_SS;
    PWM12CON = PWM_SS;
    PWM13CON = PWM_SS;
    PWM14CON = PWM_SS;
    PWM15CON = PWM_SS;

    PWM20CON = (uint8_t)(PWM_MODE_ENABLE | PWM_SS | PWM_CLK_DIV_4);
    PWM21CON = PWM_SS;
    PWM22CON = PWM_SS;
    PWM23CON = PWM_SS;
    PWM24CON = PWM_SS;
    PWM25CON = PWM_SS;
}

void indicators_pwm_disable(void)
{
    PWM00CON = PWM_CON_PARKED;
    PWM01CON = PWM_CON_OFF;
    PWM02CON = PWM_CON_OFF;
    PWM03CON = PWM_CON_OFF;
    PWM04CON = PWM_CON_OFF;
    PWM05CON = PWM_CON_OFF;
    PWM10CON = PWM_CON_PARKED;
    PWM11CON = PWM_CON_OFF;
    PWM12CON = PWM_CON_OFF;
    PWM13CON = PWM_CON_OFF;
    PWM14CON = PWM_CON_OFF;
    PWM15CON = PWM_CON_OFF;
    PWM20CON = PWM_CON_PARKED;
    PWM21CON = PWM_CON_OFF;
    PWM22CON = PWM_CON_OFF;
    PWM23CON = PWM_CON_OFF;
    PWM24CON = PWM_CON_OFF;
    PWM25CON = PWM_CON_OFF;

    GPIO_LOW(1, VDR_P1_MASK);
    GPIO_LOW(2, VDR_P2_MASK);
    GPIO_LOW(3, VDR_P3_MASK);
    user_matrix_cols_deselect_all();
}

uint8_t indicators_via_enabled(void)
{
    return user_settings.led_effect < FX_OFF;
}

uint8_t indicators_via_effect(void)
{
    return user_settings.led_effect;
}

uint8_t indicators_via_brightness(void)
{
    return user_settings.led_brightness;
}

uint8_t indicators_via_speed(void)
{
    return user_settings.led_speed;
}

void indicators_via_set_enabled(uint8_t enabled)
{
    uint8_t previous = user_settings.led_effect;
    if (enabled) {
        if (user_settings.led_effect >= FX_OFF) user_settings.led_effect = FX_RADIAL;
    } else {
        user_settings.led_effect = FX_OFF;
    }
    if (user_settings.led_effect != previous) {
        render_dirty = true;
        settings_mark_dirty();
    }
}

void indicators_via_set_effect(uint8_t effect)
{
    if (effect <= FX_OFF) {
        if (effect == FX_REACTIVE && user_settings.led_effect != FX_REACTIVE) reactive_clear();
        if (user_settings.led_effect != effect) {
            user_settings.led_effect = effect;
            render_dirty = true;
            settings_mark_dirty();
        }
    }
}

void indicators_via_set_brightness(uint8_t brightness)
{
    if (user_settings.led_brightness != brightness) {
        user_settings.led_brightness = brightness;
        render_dirty = true;
        settings_mark_dirty();
    }
}

void indicators_via_set_speed(uint8_t speed)
{
    if (speed < LED_SPEED_MIN) speed = LED_SPEED_MIN;
    if (speed > LED_SPEED_MAX) speed = LED_SPEED_MAX;
    if (user_settings.led_speed != speed) {
        user_settings.led_speed = speed;
        settings_mark_dirty();
    }
}

void indicators_next_effect(void)
{
    uint8_t previous = user_settings.led_effect;
    if (++user_settings.led_effect > FX_OFF) {
        user_settings.led_effect = 0;
    }
    if (user_settings.led_effect == FX_REACTIVE && previous != FX_REACTIVE) reactive_clear();
    render_dirty = true;
    settings_mark_dirty();
}

void indicators_brightness_up(void)
{
    if (user_settings.led_brightness <= (uint8_t)(255u - LED_BRIGHTNESS_STEP)) {
        user_settings.led_brightness += LED_BRIGHTNESS_STEP;
        render_dirty = true;
        settings_mark_dirty();
    }
}

void indicators_brightness_down(void)
{
    uint8_t previous = user_settings.led_brightness;
    if (user_settings.led_brightness >= LED_BRIGHTNESS_STEP) {
        user_settings.led_brightness -= LED_BRIGHTNESS_STEP;
    } else {
        user_settings.led_brightness = 0;
    }
    if (user_settings.led_brightness != previous) {
        render_dirty = true;
        settings_mark_dirty();
    }
}

void indicators_speed_up(void)
{
    if (user_settings.led_speed < LED_SPEED_MAX) {
        user_settings.led_speed++;
        settings_mark_dirty();
    }
}

void indicators_speed_down(void)
{
    if (user_settings.led_speed > LED_SPEED_MIN) {
        user_settings.led_speed--;
        settings_mark_dirty();
    }
}

void settings_save_pre(void)
{
    tick_pause();
    indicators_pwm_disable();
}

void settings_save_post(void)
{
    tick_resume();
}
