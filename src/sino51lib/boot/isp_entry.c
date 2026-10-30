#pragma codeseg BOOTISP

#include "boot.h"
#include "sfr.h"

#ifndef EA_BIT_ASM
#    error EA_BIT_ASM must be defined
#endif

/*
 * The application-facing door into the bootloader, at the address every
 * SinoWealth ISP bootloader has used: `ljmp ISP_ENTRY` with A = 0x5a, B = 0xa5.
 * Keeping it means stock firmware, and anything that drives it, reaches this
 * bootloader the way it reached the one it replaced.
 *
 * It only leaves a request behind and restarts at BOOT_BASE, so the request
 * runs through the same startup as a cold boot. A wrong key falls through to
 * the same restart, which is a plain reboot: nothing here can be tricked into
 * ISP mode without the key.
 *
 * The linker puts this area at ISP_ENTRY (see the boot build's -Wl-bBOOTISP),
 * and --code-size keeps the rest of the bootloader below it.
 */
void boot_isp_entry(void) __naked
{
    // clang-format off
    __asm
        clr  EA_BIT_ASM

        xrl  a, #ISP_KEY_A
        jnz  00001$
        mov  a, b
        xrl  a, #ISP_KEY_B
        jnz  00001$

        mov  dptr, #BOOT_REQUEST_ADDR_ASM
        mov  a, #BOOT_REQUEST_MAGIC_0
        movx @dptr, a
        inc  dptr
        mov  a, #BOOT_REQUEST_MAGIC_1
        movx @dptr, a
        inc  dptr
        mov  a, #BOOT_REQUEST_MAGIC_2
        movx @dptr, a
        inc  dptr
        mov  a, #BOOT_REQUEST_MAGIC_3
        movx @dptr, a
    00001$:
        ljmp BOOT_BASE_ASM
    __endasm;
    // clang-format on
}
