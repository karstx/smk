// SMK mouse firmware for the ISY IGM3000 (SH68F881).
//
// Safety first: the only way to reflash this mouse is USB ISP, so
//  1. the stock-compatible ISP feature report (id 5, cmd 0x75, interface 1) is
//     always served -> unmodified sinowisp can switch it to the bootloader;
//  2. holding >= 2 buttons while plugging in jumps straight to the bootloader;
//  3. if USB never reaches CONFIGURED within ENUM_FALLBACK_MS, it jumps too;
//  4. nothing here writes flash or EEPROM.
#include "board.h"
#include "clock.h"
#include "delay.h"
#include "input.h"
#include "isp.h"
#include "ldo.h"
#include "led.h"
#include "peripherals.h"
#include "sensor.h"
#include "usb.h"
#include "usbhw.h"
#include "watchdog.h"
#include <stdint.h>

void usb_interrupt_handler(void) __interrupt(USB_VECTOR); // defined in usbhw.c
void timer2_isr(void) __interrupt(5);                     // _INT_TIMER2

volatile uint16_t ms_ticks;
volatile __bit    ms_flag;
static __bit      half_tick;

static const __code uint8_t button_map[8] = BUTTON_MAP_INIT;

static uint8_t  dpi_level;
static uint16_t boot_ms;
static uint16_t motion_events;
static int16_t  acc_x, acc_y;
static uint8_t  last_mouse_buttons;
static uint8_t  prev_stable;
static int8_t   last_dx, last_dy;
static uint8_t  led_div;

// ---------------------------------------------------------------- timebase

void timer2_isr(void) __interrupt(5)
{
    uint8_t saved = INSCON;
    sfr_page_0(); // T2CON shares its address with T4CON on page 1
    TF2       = 0;
    half_tick = !half_tick;
    if (!half_tick) {
        ms_ticks++;
        ms_flag = 1;
    }
    INSCON = saved;
}

static void timer2_init(void)
{
    // 0.5 ms period. Timer2 counts at FREQ_SYS/6 = 2 MHz on this part: measured on
    // the IGM3000 (smk's 0xFF7D reload ran 7.6x fast here), so 1000 counts.
    TR2    = 0;
    T2CON  = 0;
    RCAP2H = 0xfc;
    RCAP2L = 0x18;
    TH2    = 0xfc;
    TL2    = 0x18;
    TF2    = 0;
    ET2    = 1;
    TR2    = 1;
}

// ---------------------------------------------------------------- GPIO (stock 0x2DAE)

static void gpio_init(void)
{
    sfr_page_0();
    P0    = 0;
    P0PCR = 0xd1;
    P0CR  = 0;
    P1    = 0;
    P1PCR = 0xff;
    P1CR  = 0;
    P2    = 0;
    P2PCR = 0xff;
    P2CR  = 0;
    P3    = 0;
    P3PCR = 0xff;
    P3CR  = 0;
    P4    = 0;
    P4PCR = 0xff;
    P4CR  = 0;

    sfr_page_1();
    P5    = 0; // page-1 aliases: P0 address = P5 etc.
    P5PCR = 0xff;
    P5CR  = 0;
    P6    = 0;
    P6PCR = 0xff;
    P6CR  = 0;
    P7    = 0;
    P7PCR = 0xff;
    P7CR  = 0x01; // P7.0 output low, as stock
    P8    = 0;
    P8PCR = 0xff;
    P8CR  = 0;
    sfr_page_0();

    // stock main: P0.1 and P0.5 inputs without pull-ups
    P0PCR &= 0xfd;
    P0CR &= 0xfd;
    P0PCR &= 0xdf;
    P0CR &= 0xdf;
}

static uint8_t popcount(uint8_t v)
{
    uint8_t n = 0;
    while (v) {
        n += v & 1;
        v >>= 1;
    }
    return n;
}

static void boot_chord_check(void)
{
    delay_ms(10);
    uint8_t a = buttons_scan_raw();
    delay_ms(10);
    uint8_t b = buttons_scan_raw();
    if (popcount(a & b) >= BOOT_CHORD_MIN_BUTTONS) {
        isp_jump();
    }
}

// ---------------------------------------------------------------- behaviour

static void set_dpi(uint8_t level)
{
    if (level >= sensor_dpi_levels()) {
        level = sensor_dpi_levels() - 1;
    }
    dpi_level = level;
    sensor_set_dpi_level(level);
    led_dpi_indicator(level);
}

static void handle_buttons(uint8_t stable)
{
    uint8_t pressed_edge = stable & (uint8_t)~prev_stable;
    uint8_t mouse        = 0;

    prev_stable = stable;
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t mask = (uint8_t)(1u << i);
        uint8_t act  = button_map[i];
        if (stable & mask) {
            switch (act) {
                case ACT_LEFT:
                    mouse |= 0x01;
                    break;
                case ACT_RIGHT:
                    mouse |= 0x02;
                    break;
                case ACT_MIDDLE:
                    mouse |= 0x04;
                    break;
                case ACT_BACK:
                    mouse |= 0x08;
                    break;
                case ACT_FORWARD:
                    mouse |= 0x10;
                    break;
                default:
                    break;
            }
        }
        if (pressed_edge & mask) {
            switch (act) {
                case ACT_DPI_UP:
                    set_dpi(dpi_level + 1 < sensor_dpi_levels() ? dpi_level + 1 : dpi_level);
                    break;
                case ACT_DPI_DOWN:
                    set_dpi(dpi_level ? dpi_level - 1 : 0);
                    break;
                case ACT_LED_MODE:
                    led_next_mode();
                    break;
                default:
                    break;
            }
        }
    }
    last_mouse_buttons = mouse;
}

static int16_t sat_add(int16_t a, int16_t b)
{
    int32_t s = (int32_t)a + b;
    if (s > 32767) return 32767;
    if (s < -32767) return -32767;
    return (int16_t)s;
}

static int8_t clamp8(int16_t v)
{
    return v > 127 ? 127 : (v < -127 ? -127 : (int8_t)v);
}

static void handle_command(void)
{
    switch (cmd_buf[1]) {
        case 0:
            led_clear_override();
            break;
        case 1:
            led_set_override(cmd_buf[2], cmd_buf[3], cmd_buf[4]);
            break;
        case 2:
            set_dpi(cmd_buf[2]);
            break;
        case 3:
            led_mode = cmd_buf[2] < LED_MODE_COUNT ? cmd_buf[2] : LED_MODE_RAINBOW;
            break;
        default:
            break;
    }
    cmd_pending = 0;
}

static void update_diag(void)
{
    diag[1]  = sensor_type;
    diag[2]  = sensor_pid;
    diag[3]  = buttons_raw;
    diag[4]  = buttons_stable;
    diag[5]  = (uint8_t)wheel_total;
    diag[6]  = dpi_level;
    diag[7]  = sensor_dpi_code;
    diag[8]  = led_mode;
    diag[9]  = led_override;
    diag[10] = (usb_is_configured() ? 1 : 0) | (usb_ever_configured() ? 2 : 0) | (usb_is_suspended() ? 4 : 0) | (usb_boot_protocol() ? 8 : 0);
    diag[11] = FW_VERSION;
    diag[12] = (uint8_t)motion_events;
    diag[13] = (uint8_t)(motion_events >> 8);
    diag[14] = (uint8_t)last_dx;
    diag[15] = (uint8_t)last_dy;
    uint16_t now;
    __critical
    {
        now = ms_ticks;
    }
    diag[16] = (uint8_t)now;
    diag[17] = (uint8_t)(now >> 8);
}

static uint8_t btn_changed;

// Read the sensor as often as the main loop allows; motion accumulates until the
// host has collected the previous report, so every 1 kHz poll gets fresh data.
static void motion_task(void)
{
    int16_t dx, dy;
    if (sensor_poll(&dx, &dy)) {
        acc_x   = sat_add(acc_x, dx);
        acc_y   = sat_add(acc_y, dy);
        last_dx = clamp8(dx);
        last_dy = clamp8(dy);
        motion_events++;
    }
}

static void report_task(void)
{
    if (!usb_is_configured() || !usb_ep1_free()) {
        return;
    }
    if (btn_changed || acc_x || acc_y || wheel_accum) {
        int8_t w = wheel_accum;
        usb_send_mouse(last_mouse_buttons, acc_x, acc_y, w);
        acc_x = 0;
        acc_y = 0;
        wheel_accum -= w;
        btn_changed = 0;
    }
}

static void every_ms(void)
{
    // safety fallback: never configured -> hand over to the ISP bootloader
#if ENUM_FALLBACK_MS
    if (!usb_ever_configured()) {
        if (boot_ms < 0xffff) {
            boot_ms++;
        }
        if (boot_ms >= ENUM_FALLBACK_MS) {
            isp_jump();
        }
    }
#endif

    if (buttons_task()) {
        uint8_t before = last_mouse_buttons;
        handle_buttons(buttons_stable);
        if (before != last_mouse_buttons) {
            btn_changed = 1;
        }
    }

    if (++led_div >= 10) {
        led_div  = 0;
        led_dark = usb_is_suspended();
        led_task_10ms();
    }

    update_diag();
}

// ---------------------------------------------------------------- entry

// The stock firmware enters the ISP bootloader from inside its (highest
// priority) USB interrupt, and the bootloader never executes RETI: it starts
// this application by jumping. The CPU then still believes an interrupt is in
// service, which blocks every interrupt until a RETI. Execute RETIs (one per
// possible priority level) to clear any stale in-service state.
static void clear_interrupt_latches(void) __naked
{
    // clang-format off
    __asm
        mov     dptr, #00001$
        push    dpl
        push    dph
        reti
00001$:
        mov     dptr, #00002$
        push    dpl
        push    dph
        reti
00002$:
        mov     dptr, #00003$
        push    dpl
        push    dph
        reti
00003$:
        mov     dptr, #00004$
        push    dpl
        push    dph
        reti
00004$:
        ret
    __endasm;
    // clang-format on
}

void main(void)
{
    EA = 0;
    clear_interrupt_latches();
    ldo_init();
    clock_init();
    peripherals_init();
    gpio_init();
    input_init();
    watchdog_kick();

    boot_chord_check(); // >= 2 buttons held: straight to the bootloader

    led_init();
    timer2_init();
    usb_init();
    EA = 1;

    sensor_init(); // probe takes up to ~0.4 s; USB enumerates from the ISR meanwhile
    set_dpi(2);

    for (;;) {
        watchdog_kick();
        usb_task();
        wheel_task();
        motion_task();
        report_task();
        if (cmd_pending) {
            handle_command();
        }
        if (ms_flag) {
            ms_flag = 0;
            every_ms();
        }
    }
}
