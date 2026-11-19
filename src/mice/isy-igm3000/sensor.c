// Optical sensor driver, ported from the stock IGM3000 firmware.
//
// The stock firmware supports four sensor/PCB variants and probes for them at
// every boot (stock 0x214F); this does the same, in the same order.
//
// Bus: NCS P2.4, SCK P2.6, data on P2.5 (MOSI, type 2 only) and P2.7.
// Every line is driven "open-drain style" exactly like stock: low = output low,
// high = input with the port pull-up (P2PCR bits 4..7). Never drive these high
// push-pull except where stock does (type 1 setup).

#include "sensor.h"
#include "board.h"
#include "delay.h"
#include "watchdog.h"

uint8_t sensor_type;
uint8_t sensor_pid;
uint8_t sensor_dpi_code;

static __bit   bus_mosi_p25; // 1: write data on P2.5 (4-wire), 0: on P2.7 (3-wire)
static uint8_t t_srad_us;    // address -> data delay for reads

// ---- pin primitives (all on SFR page 0) ----

#define NCS_LOW()     \
    do {              \
        P2_4 = 0;     \
        P2CR |= 0x10; \
        P2_4 = 0;     \
    } while (0)
#define NCS_HIGH()              \
    do {                        \
        P2CR &= (uint8_t)~0x10; \
        P2_4 = 1;               \
    } while (0)
#define SCK_LOW()     \
    do {              \
        P2_6 = 0;     \
        P2CR |= 0x40; \
        P2_6 = 0;     \
    } while (0)
#define SCK_HIGH()              \
    do {                        \
        P2CR &= (uint8_t)~0x40; \
        P2_6 = 1;               \
    } while (0)
#define P25_LOW()     \
    do {              \
        P2_5 = 0;     \
        P2CR |= 0x20; \
        P2_5 = 0;     \
    } while (0)
#define P25_RELEASE()           \
    do {                        \
        P2CR &= (uint8_t)~0x20; \
        P2_5 = 1;               \
    } while (0)
#define P27_LOW()     \
    do {              \
        P2_7 = 0;     \
        P2CR |= 0x80; \
        P2_7 = 0;     \
    } while (0)
#define P27_RELEASE()           \
    do {                        \
        P2CR &= (uint8_t)~0x80; \
        P2_7 = 1;               \
    } while (0)

// Lines are pulled up, not driven, so give the rising edge time to settle.
#define SETTLE() \
    do {         \
        NOP();   \
        NOP();   \
        NOP();   \
        NOP();   \
        NOP();   \
        NOP();   \
    } while (0)

static void bus_idle(void)
{
    sfr_page_0();
    // stock 0x3FBF / 0x3FA8: release everything, pull-ups on P2.4..P2.7
    NCS_HIGH();
    SCK_HIGH();
    P2CR &= 0x0F;
    P2PCR |= 0xF0;
    P2_5 = 1;
    P2_7 = 1;
}

static void tx_byte(uint8_t v)
{
    for (uint8_t i = 0; i < 8; i++) {
        SCK_LOW();
        if (bus_mosi_p25) {
            if (v & 0x80) {
                P25_RELEASE();
            } else {
                P25_LOW();
            }
        } else {
            if (v & 0x80) {
                P27_RELEASE();
            } else {
                P27_LOW();
            }
        }
        v <<= 1;
        SETTLE();
        SCK_HIGH();
        SETTLE();
    }
}

static uint8_t rx_byte(void)
{
    uint8_t v = 0;
    for (uint8_t i = 0; i < 8; i++) {
        SCK_LOW();
        v <<= 1;
        SETTLE();
        SCK_HIGH();
        SETTLE();
        if (P2_7) {
            v |= 1;
        }
    }
    return v;
}

static void write_reg(uint8_t reg, uint8_t val)
{
    NCS_LOW();
    tx_byte(reg | 0x80);
    delay_us(2);
    tx_byte(val);
    delay_us(2);
    NCS_HIGH();
    if (bus_mosi_p25) {
        P25_LOW(); // stock 0x286E: MOSI parked low after a write
    } else {
        P27_RELEASE();
    }
    delay_us(60); // stock waits 0x0F..0x32 loop units (18..60 us)
}

static uint8_t read_reg(uint8_t reg)
{
    NCS_LOW();
    tx_byte(reg & 0x7f);
    P27_RELEASE(); // data line becomes the sensor's
    delay_us(t_srad_us);
    uint8_t v = rx_byte();
    delay_us(2);
    NCS_HIGH();
    delay_us(20);
    return v;
}

// stock 0x3F91: 10 x ~3.8 ms with watchdog kicks
static void wait_38ms(void)
{
    delay_ms(38); // delay_ms kicks the watchdog while it spins
}

static void use_bus(uint8_t mosi_p25, uint8_t srad)
{
    bus_mosi_p25 = mosi_p25;
    t_srad_us    = srad;
}

static uint8_t probe_pid(uint8_t want)
{
    for (uint8_t i = 0; i < 10; i++) {
        watchdog_kick();
        uint8_t pid = read_reg(0x00);
        if (pid == want) {
            sensor_pid = pid;
            return 1;
        }
    }
    return 0;
}

// Type 1 setup (stock 0x3C79 / 0x3BBC): pulse P2.5 low, then hold it high
// push-pull (likely the sensor's reset/power line on that PCB variant).
static void type1_power_pulse(void)
{
    P25_LOW();
    delay_us(12);
    P2_5 = 1;
    P2CR |= 0x20;
    P2_5 = 1;
}

static void detect(void)
{
    sensor_type = SENSOR_NONE;
    sensor_pid  = 0;

    // type 2: 4-wire, PID 0x09
    bus_idle();
    delay_us(60);
    use_bus(1, 20);
    write_reg(0x3a, 0x5a);
    wait_38ms();
    wait_38ms();
    if (probe_pid(0x09)) {
        sensor_type = SENSOR_T2_0x09;
        return;
    }

    // type 1: 3-wire, PID 0x12
    bus_idle();
    type1_power_pulse();
    P2_7 = 1;
    delay_us(60);
    use_bus(0, 10);
    write_reg(0x3a, 0x5a);
    wait_38ms();
    wait_38ms();
    if (probe_pid(0x12)) {
        sensor_type = SENSOR_T1_0x12;
        return;
    }

    // type 4: 3-wire, PID 0x3B
    bus_idle();
    use_bus(0, 60);
    write_reg(0x3a, 0x5a);
    wait_38ms();
    wait_38ms();
    if (probe_pid(0x3b)) {
        sensor_type = SENSOR_T4_0x3B;
        return;
    }

    // type 11: 3-wire, PID 0x43
    bus_idle();
    use_bus(0, 60);
    write_reg(0x3a, 0x5a);
    delay_us(1200);
    write_reg(0x18, 0x39);
    wait_38ms();
    wait_38ms();
    if (probe_pid(0x43)) {
        sensor_type = SENSOR_T11_0x43;
        return;
    }
}

// stock 0x3A64
static const __code uint8_t init_t4[][2] = {
    {0x1d, 0x0a}, {0x14, 0x40}, {0x18, 0x40}, {0x34, 0x28}, {0x64, 0x32}, {0x65, 0x32}, {0x66, 0x26}, {0x67, 0x26}, {0x21, 0x04}, {0x1e, 0x78}, {0x28, 0x58}, {0x3d, 0x3d}, {0x16, 0xf0}, {0x22, 0x80}, {0x40, 0x04}, {0x39, 0x81}, {0x36, 0x05}, {0x37, 0x00},
};

// stock 0x2EDB
static const __code uint8_t init_t11[][2] = {
    {0x78, 0x80}, {0x79, 0x80}, {0x14, 0x80}, {0x20, 0x40}, {0x1a, 0x40}, {0x47, 0x00}, {0x48, 0x01}, {0x60, 0x01}, {0x69, 0x03}, {0x1d, 0x90}, {0x1b, 0x2e}, {0x24, 0x05}, {0x56, 0x00}, {0x2c, 0x8a}, {0x2d, 0x58}, {0x40, 0x80}, {0x7f, 0x01}, {0x7a, 0x32}, {0x6a, 0x93}, {0x6b, 0x68}, {0x6c, 0x71}, {0x6d, 0x50}, {0x7f, 0x00}, {0x7f, 0x02}, {0x29, 0x1c}, {0x2a, 0x1a}, {0x2b, 0x90}, {0x40, 0x80}, {0x7f, 0x00},
};

static void init_type11(void)
{
    for (uint8_t r = 2; r <= 6; r++) {
        (void)read_reg(r);
    }
    for (uint8_t i = 0; i < sizeof(init_t11) / 2; i++) {
        watchdog_kick();
        write_reg(init_t11[i][0], init_t11[i][1]);
    }
}

static void post_detect_init(void)
{
    switch (sensor_type) {
        case SENSOR_T1_0x12: // stock 0x3BBC
            type1_power_pulse();
            delay_us(800);
            write_reg(0x3a, 0x5a);
            wait_38ms();
            wait_38ms();
            write_reg(0x22, 0x80);
            break;
        case SENSOR_T2_0x09: // stock 0x39AB
            write_reg(0x41, 0x00);
            delay_us(180);
            write_reg(0x22, 0x80);
            delay_us(180);
            write_reg(0x40, 0x04);
            delay_us(180);
            write_reg(0x43, 0x09);
            delay_us(180);
            break;
        case SENSOR_T4_0x3B: // stock 0x3BEC
            for (uint8_t i = 0; i < sizeof(init_t4) / 2; i++) {
                watchdog_kick();
                write_reg(init_t4[i][0], init_t4[i][1]);
            }
            break;
        case SENSOR_T11_0x43: // stock 0x34BE
            init_type11();
            break;
        default:
            break;
    }
}

// ---- DPI ----
// CPI register value = table[code]; codes at/above the threshold also double the
// motion in software (stock behaviour beyond the sensor's native range).

static const __code uint8_t cpi_t2[]  = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1a, 0x1c, 0x17, 0x18};
static const __code uint8_t cpi_t1[]  = {0x12, 0x14, 0x16, 0x18, 0x1a, 0x1b, 0x16, 0x17, 0x18, 0x1a, 0x1b};
static const __code uint8_t cpi_t4[]  = {0x81, 0x82, 0x83, 0x80, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x88, 0x89, 0x8a, 0x8c, 0x8e};
static const __code uint8_t cpi_t11[] = {0x00, 0x04, 0x06, 0x08, 0x0b, 0x0d, 0x0f, 0x12, 0x14, 0x16, 0x19, 0x1b, 0x1d, 0x20, 0x22, 0x24, 0x27, 0x29, 0x2b, 0x2e, 0x30, 0x32, 0x34, 0x37, 0x39, 0x3b, 0x3e, 0x40, 0x42, 0x45, 0x47, 0x49, 0x4c, 0x4e, 0x50, 0x53, 0x55, 0x57, 0x5a, 0x5c, 0x5e, 0x61, 0x63, 0x65, 0x68, 0x6a, 0x6c, 0x6f, 0x71, 0x73};

#define DPI_LEVELS 6
static const __code uint8_t levels_t1[DPI_LEVELS]  = {0, 1, 2, 4, 5, 8};
static const __code uint8_t levels_t2[DPI_LEVELS]  = {0, 2, 4, 7, 9, 11};
static const __code uint8_t levels_t4[DPI_LEVELS]  = {0, 4, 8, 11, 13, 16};
static const __code uint8_t levels_t11[DPI_LEVELS] = {4, 9, 18, 27, 36, 45};

static __bit dpi_double;

uint8_t sensor_dpi_levels(void)
{
    return DPI_LEVELS;
}

void sensor_set_dpi_level(uint8_t level)
{
    if (level >= DPI_LEVELS) {
        level = DPI_LEVELS - 1;
    }
    switch (sensor_type) {
        case SENSOR_T1_0x12:
            sensor_dpi_code = levels_t1[level];
            dpi_double      = sensor_dpi_code > 5;
            write_reg(0x19, cpi_t1[sensor_dpi_code]);
            break;
        case SENSOR_T2_0x09:
            sensor_dpi_code = levels_t2[level];
            dpi_double      = sensor_dpi_code >= 10;
            write_reg(0x33, cpi_t2[sensor_dpi_code]);
            break;
        case SENSOR_T4_0x3B:
            sensor_dpi_code = levels_t4[level];
            dpi_double      = sensor_dpi_code > 13;
            write_reg(0x0d, cpi_t4[sensor_dpi_code]);
            break;
        case SENSOR_T11_0x43:
            sensor_dpi_code = levels_t11[level];
            dpi_double      = 0;
            write_reg(0x1b, cpi_t11[sensor_dpi_code]);
            break;
        default:
            break;
    }
}

void sensor_init(void)
{
    detect();
    post_detect_init();
}

static int16_t sat_double(int16_t v)
{
    if (v > 16383) {
        return 32767;
    }
    if (v < -16383) {
        return -32767;
    }
    return v * 2;
}

uint8_t sensor_poll(int16_t *dx, int16_t *dy)
{
    int16_t x, y;

    sfr_page_0();
    switch (sensor_type) {
        case SENSOR_T1_0x12:
        case SENSOR_T2_0x09:
            if (!(read_reg(0x02) & 0x80)) {
                return 0;
            }
            x = (int8_t)read_reg(0x03);
            y = (int8_t)read_reg(0x04);
            break;

        case SENSOR_T4_0x3B: {
            if (!(read_reg(0x02) & 0x80)) {
                return 0;
            }
            uint8_t xl = read_reg(0x03);
            uint8_t yl = read_reg(0x04);
            uint8_t hi = read_reg(0x0c);
            x          = (int16_t)(((uint16_t)(hi & 0xf0) << 4) | xl);
            y          = (int16_t)(((uint16_t)(hi & 0x0f) << 8) | yl);
            if (x & 0x0800) {
                x |= (int16_t)0xf000;
            }
            if (y & 0x0800) {
                y |= (int16_t)0xf000;
            }
            break;
        }

        case SENSOR_T11_0x43: {
            uint8_t b[6];
            NCS_LOW();
            tx_byte(0x16); // motion burst
            P27_RELEASE();
            delay_us(t_srad_us);
            for (uint8_t i = 0; i < 6; i++) {
                b[i] = rx_byte();
            }
            delay_us(2);
            NCS_HIGH();
            delay_us(20);
            if (!(b[0] & 0x80)) {
                return 0;
            }
            if (b[1] == 0) { // stock 0x288C: sensor lost its config, redo init
                init_type11();
                return 0;
            }
            x = (int16_t)(((uint16_t)b[3] << 8) | b[2]);
            y = (int16_t)(((uint16_t)b[5] << 8) | b[4]);
            break;
        }

        default:
            return 0;
    }

    if (x == (int16_t)0x8000) {
        x = -32767;
    }
    if (y == (int16_t)0x8000) {
        y = -32767;
    }
    if (dpi_double) {
        x = sat_double(x);
        y = sat_double(y);
    }
    // stock 0x17D2, orientation 0: Y negated for every type except type 11
    if (sensor_type != SENSOR_T11_0x43) {
        y = -y;
    }
    *dx = x;
    *dy = y;
    return (x | y) != 0;
}
