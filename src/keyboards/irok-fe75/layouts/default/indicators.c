#include "indicators.h"
#include "kbdef.h"
#include "gpio.h"
#include "pwm.h"
#include "settings.h"
#include "led_effect.h"
#include "user_led.h"
#include "user_matrix.h"
#include <string.h>

// LEDs sit on the key matrix: a key column driven low is the common for its six
// LEDs, and 18 PWM channels on P1-P3 carry R/G/B for each of the six rows. One
// LED subframe lights one column. Channel order and duty staggering are taken
// from the stock firmware (scan ISR at 0x266A, PWM setup at 0x6F01).

#define LED_ROWS MATRIX_ROWS
#define LED_COLS 16 // commons C0-C15; C16-C20 have no LEDs on the 75%

#define LED_SPEED_DEFAULT 4
#define LED_SPEED_MIN     1
#define LED_SPEED_MAX     16

#define LED_BRIGHTNESS_DEFAULT 128
#define LED_BRIGHTNESS_STEP    32

// Board effects (stored in user_settings.led_effect). The shared FX_* set all
// sweep the full colour wheel, which reads as the same rainbow on 16 columns.
typedef enum {
    IROK_FX_WAVE = 0, // rainbow moving across the columns
    IROK_FX_RIPPLE,   // rainbow rings moving out from the centre
    IROK_FX_CYCLE,    // whole board one colour, cycling through the wheel
    IROK_FX_BREATHE,  // cycling colour that fades in and out
    IROK_FX_STATIC,   // still rainbow across the columns
    IROK_FX_WHITE,    // solid white
    IROK_FX_BURST,    // dark board, a ring of colour bursts out from each key press
    IROK_FX_COUNT
} irok_fx_t;

#define IROK_FX_OFF IROK_FX_COUNT

// Animation clock: one step every 64 LED subframes (~32 ms, as the 400 us
// subframes plus the key sweeps between them add up). Tied to subframes rather
// than frames so changing the refresh rate no longer changes effect speed.
#define LED_STEP_SUBFRAMES 64

// Key bursts: how many can overlap, and how many steps one lasts (~0.5 s)
#define BURST_SLOTS 4
#define BURST_STEPS 16

#define PWM_PERD 0x0438 // stock period: 1080 counts at SYS_CLK / 4

// PWM on/off values, as stock writes them
#define PWM_CON_LEAD (uint8_t)(PWM_MODE_ENABLE | PWM_SS | PWM_CLK_DIV_4)
#define PWM_CON_CHAN PWM_SS
#define PWM_CON_IDLE (uint8_t)(PWM_MOD | PWM_CLK_DIV_4)

#include LED_GEOMETRY_HEADER
_Static_assert(LED_GEOMETRY_ROWS == LED_ROWS && LED_GEOMETRY_COLS == LED_COLS, "generated LED geometry size does not match the LED matrix");

// [column][row * 3 + colour slot]
static uint8_t led_fb[LED_COLS][LED_ROWS * 3];

// LEDs follow their key's column except in the shift row, where Z sits on LED
// common C12 (stock table at 0x1844). Effects are drawn by key position, so the
// LED at (row, led col) is drawn as the key column below.
static const __code uint8_t led_key_col_r4[LED_COLS] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 1, 13, 14, 15};

typedef struct {
    uint8_t row;
    uint8_t col; // key column
    uint8_t age; // steps since the press; BURST_STEPS = idle
    uint8_t hue;
} burst_t;

static burst_t bursts[BURST_SLOTS];
static uint8_t burst_next;

static uint8_t led_col;
static uint8_t led_phase;
static uint8_t anim_ctr;
static uint8_t regen_row;
static uint8_t regen_col;

static volatile bool render_dirty;

static uint8_t last_effect; // restored by indicators_toggle()

void        indicators_pwm_enable();
void        indicators_pwm_disable();
static void led_regen_one();
static bool led_load_channels();
static void led_select_col(uint8_t col);

void indicators_apply_defaults()
{
    user_settings.led_effect     = IROK_FX_WAVE;
    user_settings.led_brightness = LED_BRIGHTNESS_DEFAULT;
    user_settings.led_speed      = LED_SPEED_DEFAULT;
#ifdef RF_ENABLED
    user_settings.rf_link = 1; // RF_MODE_BT1: the BT host the slider's BT position starts on
#endif
}

void indicators_validate_settings()
{
    if (user_settings.led_effect > IROK_FX_OFF) {
        user_settings.led_effect = IROK_FX_WAVE;
    }
    if (user_settings.led_speed < LED_SPEED_MIN) {
        user_settings.led_speed = LED_SPEED_MIN;
    }
    if (user_settings.led_speed > LED_SPEED_MAX) {
        user_settings.led_speed = LED_SPEED_MAX;
    }
}

void indicators_init()
{
    memset(led_fb, 0, sizeof(led_fb));

    PWM0PERDH = (uint8_t)(PWM_PERD >> 8);
    PWM0PERDL = (uint8_t)(PWM_PERD);
    PWM1PERDH = (uint8_t)(PWM_PERD >> 8);
    PWM1PERDL = (uint8_t)(PWM_PERD);
    PWM2PERDH = (uint8_t)(PWM_PERD >> 8);
    PWM2PERDL = (uint8_t)(PWM_PERD);

    indicators_pwm_disable();
}

void indicators_start()
{
    led_col    = 0;
    led_phase  = 0;
    anim_ctr   = 0;
    burst_next = 0;
    for (uint8_t i = 0; i < BURST_SLOTS; i++) {
        bursts[i].age = BURST_STEPS;
    }
    regen_row    = 0;
    regen_col    = 0;
    render_dirty = true;
}

void indicators_factory_reset()
{
    indicators_apply_defaults();
    settings_mark_dirty();
}

void indicators_toggle()
{
    if (user_settings.led_effect == IROK_FX_OFF) {
        user_settings.led_effect = last_effect;
    } else {
        last_effect              = user_settings.led_effect;
        user_settings.led_effect = IROK_FX_OFF;
    }
    settings_mark_dirty();
}

void indicators_next_effect()
{
    // off is Fn+F4's job; from off, F12 turns the backlight back on
    if (user_settings.led_effect >= (uint8_t)(IROK_FX_COUNT - 1)) {
        user_settings.led_effect = 0;
    } else {
        user_settings.led_effect++;
    }
    settings_mark_dirty();
}

void indicators_brightness_up()
{
    if (user_settings.led_brightness > (uint8_t)(255 - LED_BRIGHTNESS_STEP)) {
        user_settings.led_brightness = 255;
    } else {
        user_settings.led_brightness = (uint8_t)(user_settings.led_brightness + LED_BRIGHTNESS_STEP);
    }
    settings_mark_dirty();
}

void indicators_brightness_down()
{
    if (user_settings.led_brightness < LED_BRIGHTNESS_STEP) {
        user_settings.led_brightness = 0;
    } else {
        user_settings.led_brightness = (uint8_t)(user_settings.led_brightness - LED_BRIGHTNESS_STEP);
    }
    settings_mark_dirty();
}

void indicators_speed_up()
{
    if (user_settings.led_speed < LED_SPEED_MAX) {
        user_settings.led_speed++;
    }
    settings_mark_dirty();
}

void indicators_speed_down()
{
    if (user_settings.led_speed > LED_SPEED_MIN) {
        user_settings.led_speed--;
    }
    settings_mark_dirty();
}

void indicators_pre_update()
{
    // the previous subframe's common goes back high (dark) before the next loads
    led_select_col(0xFF);
}

// A frame is recomputed a few LEDs per main-loop pass, so the loop never stalls
// for the whole 96 and key events keep flowing to the host while it renders.
#define LED_REGEN_PER_PASS 8

static uint8_t regen_left;

void indicators_render()
{
    if (!regen_left) {
        if (!render_dirty) {
            return;
        }
        render_dirty = false;
        regen_left   = (uint8_t)(LED_ROWS * LED_COLS);
    }

    for (uint8_t i = 0; i < LED_REGEN_PER_PASS && regen_left; i++, regen_left--) {
        led_regen_one();
    }
}

bool indicators_update_step(keyboard_state_t *keyboard, uint8_t current_step)
{
    keyboard;
    current_step;

    if (++anim_ctr >= LED_STEP_SUBFRAMES) {
        anim_ctr     = 0;
        led_phase    = (uint8_t)(led_phase + user_settings.led_speed);
        render_dirty = true;
        for (uint8_t i = 0; i < BURST_SLOTS; i++) {
            if (bursts[i].age < BURST_STEPS) {
                bursts[i].age++;
            }
        }
    }

    indicators_pwm_disable();

    if (led_load_channels()) {
        led_select_col(led_col);
        indicators_pwm_enable();
    }

    bool frame_wrapped = false;
    if (++led_col >= LED_COLS) {
        led_col       = 0;
        frame_wrapped = true;
    }

    return frame_wrapped;
}

void indicators_post_update() {}

static uint8_t scale(uint8_t v, uint8_t by)
{
    return (uint8_t)(((uint16_t)v * by) >> 8);
}

void indicators_key_event(uint8_t row, uint8_t col, bool pressed)
{
    if (!pressed || user_settings.led_effect != IROK_FX_BURST) {
        return;
    }

    burst_t *b = &bursts[burst_next];
    b->row     = row;
    b->col     = col;
    b->hue     = (uint8_t)(led_phase + burst_next * 64); // a different colour per slot
    b->age     = 0;
    if (++burst_next >= BURST_SLOTS) {
        burst_next = 0;
    }
}

static uint8_t absdiff(uint8_t a, uint8_t b)
{
    return (a > b) ? (uint8_t)(a - b) : (uint8_t)(b - a);
}

// ring of colour around each live burst: radius grows half a key per step and
// the whole ring fades as it ages
static void led_burst(uint8_t row, uint8_t col, uint8_t rgb[3])
{
    uint16_t acc[3] = {0, 0, 0};

    for (uint8_t i = 0; i < BURST_SLOTS; i++) {
        const burst_t *b = &bursts[i];
        if (b->age >= BURST_STEPS) {
            continue;
        }

        // distance in half-keys: max + min/2 approximates the Euclidean distance
        const uint8_t dx   = (uint8_t)(absdiff(col, b->col) << 1);
        const uint8_t dy   = (uint8_t)(absdiff(row, b->row) << 1);
        const uint8_t dist = (dx > dy) ? (uint8_t)(dx + (dy >> 1)) : (uint8_t)(dy + (dx >> 1));
        const uint8_t off  = absdiff(dist, b->age);
        if (off > 2) {
            continue;
        }

        const uint8_t ring = (uint8_t)(255 - off * 96); // 255 on the ring, fading to its edge
        const uint8_t fade = (uint8_t)(((uint16_t)(BURST_STEPS - b->age) << 8) / (BURST_STEPS + 1));
        const uint8_t lvl  = scale(ring, fade);

        uint8_t c[3];
        led_color_wheel(b->hue, c);
        acc[0] += scale(c[0], lvl);
        acc[1] += scale(c[1], lvl);
        acc[2] += scale(c[2], lvl);
    }

    rgb[0] = (acc[0] > 255) ? 255 : (uint8_t)acc[0];
    rgb[1] = (acc[1] > 255) ? 255 : (uint8_t)acc[1];
    rgb[2] = (acc[2] > 255) ? 255 : (uint8_t)acc[2];
}

static void led_effect_irok(uint8_t row, uint8_t col, uint8_t rgb[3])
{
    const uint8_t bri = user_settings.led_brightness;

    switch (user_settings.led_effect) {
        case IROK_FX_WAVE:
            led_color_wheel((uint8_t)(user_led_axis_x(col) + led_phase), rgb);
            break;
        case IROK_FX_RIPPLE:
            led_color_wheel((uint8_t)(user_led_radial(row, col) - led_phase), rgb);
            break;
        case IROK_FX_CYCLE:
            led_color_wheel(led_phase, rgb);
            break;
        case IROK_FX_BREATHE: {
            // triangle wave at 4x the phase rate, hue drifting slowly underneath
            const uint8_t t = (uint8_t)(led_phase << 2);
            led_color_wheel((uint8_t)(led_phase >> 1), rgb);
            const uint8_t level = (t & 0x80) ? (uint8_t)(~t << 1) : (uint8_t)(t << 1);
            rgb[0]              = scale(rgb[0], level);
            rgb[1]              = scale(rgb[1], level);
            rgb[2]              = scale(rgb[2], level);
            break;
        }
        case IROK_FX_STATIC:
            led_color_wheel(user_led_axis_x(col), rgb);
            break;
        case IROK_FX_WHITE:
            rgb[0] = rgb[1] = rgb[2] = 255;
            break;
        case IROK_FX_BURST:
            led_burst(row, col, rgb);
            break;
        default:
            rgb[0] = rgb[1] = rgb[2] = 0;
            return;
    }

    rgb[0] = scale(rgb[0], bri);
    rgb[1] = scale(rgb[1], bri);
    rgb[2] = scale(rgb[2], bri);
}

static void led_regen_one()
{
    uint8_t rgb[3];

#ifdef LED_TEST_PATTERN
    // R0: slot 0, R1: slot 1, R2: slot 2, R3: all slots dim, R4/R5: off
    rgb[0] = rgb[1] = rgb[2] = 0;
    if (regen_row < 3) {
        rgb[regen_row] = 255;
    } else if (regen_row == 3) {
        rgb[0] = rgb[1] = rgb[2] = 48;
    }
#else
    const uint8_t key_col = (regen_row == 4) ? led_key_col_r4[regen_col] : regen_col;
    led_effect_irok(regen_row, key_col, rgb);
#endif

    uint8_t *px    = &led_fb[regen_col][regen_row * 3];
    px[LED_SLOT_R] = (uint8_t)(((uint16_t)rgb[0] * LED_GAIN_R) >> 8);
    px[LED_SLOT_G] = (uint8_t)(((uint16_t)rgb[1] * LED_GAIN_G) >> 8);
    px[LED_SLOT_B] = (uint8_t)(((uint16_t)rgb[2] * LED_GAIN_B) >> 8);

    if (++regen_col >= LED_COLS) {
        regen_col = 0;
        if (++regen_row >= LED_ROWS) {
            regen_row = 0;
        }
    }
}

// DUTY1 is the channel's stock stagger offset, DUTY2 = DUTY1 + on-time
#define LED_CH(pwm, k, v)                                         \
    do {                                                          \
        const uint16_t d2 = (uint16_t)(k) + ((uint16_t)(v) << 2); \
        SET_PWM_DUTY(pwm, (uint16_t)(k), d2);                     \
        lit |= (v);                                               \
    } while (0)

static bool led_load_channels()
{
    const uint8_t *px  = led_fb[led_col];
    uint8_t        lit = 0;

    LED_CH(PWM20, 0x26, px[0]);
    LED_CH(PWM21, 0x27, px[1]);
    LED_CH(PWM22, 0x28, px[2]);
    LED_CH(PWM23, 0x29, px[3]);
    LED_CH(PWM24, 0x2a, px[4]);
    LED_CH(PWM25, 0x2b, px[5]);
    LED_CH(PWM10, 0x2c, px[6]);
    LED_CH(PWM11, 0x2d, px[7]);
    LED_CH(PWM12, 0x2e, px[8]);
    LED_CH(PWM13, 0x2f, px[9]);
    LED_CH(PWM14, 0x30, px[10]);
    LED_CH(PWM15, 0x31, px[11]);
    LED_CH(PWM03, 0x32, px[12]);
    LED_CH(PWM04, 0x33, px[13]);
    LED_CH(PWM05, 0x34, px[14]);
    LED_CH(PWM00, 0x35, px[15]);
    LED_CH(PWM01, 0x36, px[16]);
    LED_CH(PWM02, 0x37, px[17]);

    return lit != 0;
}

// drive one LED common (key column) low; 0xFF releases all of them
static void led_select_col(uint8_t col)
{
    user_matrix_cols_deselect_all();
    if (col != 0xFF) {
        user_matrix_col_select(col);
    }
}

void indicators_pwm_enable()
{
    PWM00CON = PWM_CON_LEAD;
    PWM01CON = PWM_CON_CHAN;
    PWM02CON = PWM_CON_CHAN;
    PWM03CON = PWM_CON_CHAN;
    PWM04CON = PWM_CON_CHAN;
    PWM05CON = PWM_CON_CHAN;

    PWM10CON = PWM_CON_LEAD;
    PWM11CON = PWM_CON_CHAN;
    PWM12CON = PWM_CON_CHAN;
    PWM13CON = PWM_CON_CHAN;
    PWM14CON = PWM_CON_CHAN;
    PWM15CON = PWM_CON_CHAN;

    PWM20CON = PWM_CON_LEAD;
    PWM21CON = PWM_CON_CHAN;
    PWM22CON = PWM_CON_CHAN;
    PWM23CON = PWM_CON_CHAN;
    PWM24CON = PWM_CON_CHAN;
    PWM25CON = PWM_CON_CHAN;
}

void indicators_pwm_disable()
{
    PWM00CON = PWM_CON_IDLE;
    PWM01CON = PWM_CON_IDLE;
    PWM02CON = PWM_CON_IDLE;
    PWM03CON = PWM_CON_IDLE;
    PWM04CON = PWM_CON_IDLE;
    PWM05CON = PWM_CON_IDLE;

    PWM10CON = PWM_CON_IDLE;
    PWM11CON = PWM_CON_IDLE;
    PWM12CON = PWM_CON_IDLE;
    PWM13CON = PWM_CON_IDLE;
    PWM14CON = PWM_CON_IDLE;
    PWM15CON = PWM_CON_IDLE;

    PWM20CON = PWM_CON_IDLE;
    PWM21CON = PWM_CON_IDLE;
    PWM22CON = PWM_CON_IDLE;
    PWM23CON = PWM_CON_IDLE;
    PWM24CON = PWM_CON_IDLE;
    PWM25CON = PWM_CON_IDLE;
}
