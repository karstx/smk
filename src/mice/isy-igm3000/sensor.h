#pragma once
#include <stdint.h>

// Sensor variants the stock firmware probes for (stock IRAM 0x0B values).
#define SENSOR_NONE     0
#define SENSOR_T1_0x12  1  // 3-wire SDIO P2.7, PID 0x12
#define SENSOR_T2_0x09  2  // 4-wire MOSI P2.5 / MISO P2.7, PID 0x09
#define SENSOR_T4_0x3B  4  // 3-wire SDIO P2.7, PID 0x3B, 12-bit motion
#define SENSOR_T11_0x43 11 // 3-wire SDIO P2.7, PID 0x43, 16-bit burst

extern uint8_t sensor_type;
extern uint8_t sensor_pid;
extern uint8_t sensor_dpi_code;

void    sensor_init(void);                   // probe + init, ~0.5 s worst case
void    sensor_set_dpi_level(uint8_t level); // 0..SENSOR_DPI_LEVELS-1
uint8_t sensor_dpi_levels(void);
// Reads motion; returns 1 and fills dx/dy (already oriented and scaled) if moved.
uint8_t sensor_poll(int16_t *dx, int16_t *dy);
