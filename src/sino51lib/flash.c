#include "flash.h"
#include "sfr.h"
#include <stdint.h>

#define SSP_KEY_2 0x05u
#define SSP_KEY_3 0x0Au
#define SSP_KEY_4 0x09u
#define SSP_KEY_5 0x06u

#define SSP_PROGRAM 0x6Eu
#define SSP_ERASE   0xE6u

#define FLASHCON_FOR(window) (uint8_t)((window) == FLASH_DATA ? _FAC : 0u)

// while FAC is set every MOVC hits the data window, so an ISR firing here would fetch its __code reads from the wrong place.
void flash_read_into(flash_window_t window, uint16_t addr, __xdata uint8_t *dst, uint8_t len)
{
    __critical
    {
        FLASHCON = FLASHCON_FOR(window);
        for (uint8_t i = 0; i < len; i++) {
            __code uint8_t *p = (__code uint8_t *)(addr + i);
            dst[i]            = *p;
        }
        FLASHCON = 0;
    }
}

bool flash_matches(flash_window_t window, uint16_t addr, const __xdata uint8_t *src, uint8_t len)
{
    bool same = true;

    __critical
    {
        FLASHCON = FLASHCON_FOR(window);
        for (uint8_t i = 0; i < len; i++) {
            __code uint8_t *p = (__code uint8_t *)(addr + i);
            if (*p != src[i]) {
                same = false;
                break;
            }
        }
        FLASHCON = 0;
    }

    return same;
}

static void ssp_run(uint8_t flashcon, uint16_t addr, uint8_t op, uint8_t data)
{
    __critical
    {
        FLASHCON  = flashcon;
        XPAGE     = (uint8_t)(addr >> 8);
        IB_OFFSET = (uint8_t)(addr & 0xFFu);
        IB_DATA   = data;
        IB_CON1   = op;
        IB_CON2   = SSP_KEY_2;
        IB_CON3   = SSP_KEY_3;
        IB_CON4   = SSP_KEY_4;
        IB_CON5   = SSP_KEY_5;
        // clang-format off
        __asm
            nop
            nop
            nop
            nop
            nop
        __endasm;
        // clang-format on
        XPAGE    = 0;
        FLASHCON = 0;
    }
}

void flash_program_byte(flash_window_t window, uint16_t addr, uint8_t value)
{
    ssp_run(FLASHCON_FOR(window), addr, SSP_PROGRAM, value);
}

void flash_program_from(flash_window_t window, uint16_t addr, const __xdata uint8_t *src, uint8_t len)
{
    uint8_t flashcon = FLASHCON_FOR(window);

    for (uint8_t i = 0; i < len; i++) {
        ssp_run(flashcon, (uint16_t)(addr + i), SSP_PROGRAM, src[i]);
    }
}

void flash_erase(flash_window_t window, uint16_t addr)
{
    ssp_run(FLASHCON_FOR(window), addr, SSP_ERASE, 0);
}
