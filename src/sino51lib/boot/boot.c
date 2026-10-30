#include "boot.h"
#include <stdint.h>

/*
 * Set by the entry stub at ISP_ENTRY and read here, one startup later. SDCC's
 * runtime clears the relocatable segments but not an absolute one, so this
 * survives the jump; four bytes of magic keep the garbage a cold boot leaves
 * in XRAM from reading as a request.
 */
// clang-format off
static __xdata __at(BOOT_REQUEST_ADDR) uint8_t request[BOOT_REQUEST_LEN];
// clang-format on

static const __code uint8_t request_magic[BOOT_REQUEST_LEN] = BOOT_REQUEST_MAGIC;

uint8_t boot_flash_byte(uint16_t addr)
{
    return *(__code uint8_t *)addr;
}

bool boot_app_enabled(void)
{
    return boot_flash_byte(APP_VECTOR_ADDR) == LJMP_OPCODE;
}

bool boot_isp_requested(void)
{
    bool match = true;

    for (uint8_t i = 0; i < BOOT_REQUEST_LEN; i++) {
        match      = match && (request[i] == request_magic[i]);
        request[i] = 0;
    }

    return match;
}

void boot_enter_app(void) __naked
{
    // clang-format off
    __asm
        ljmp APP_VECTOR_ADDR_ASM
    __endasm;
    // clang-format on
}
