#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef FLASH_FW_SIZE
#    error FLASH_FW_SIZE must be defined
#endif
#ifndef FLASH_SECTOR_SIZE
#    error FLASH_SECTOR_SIZE must be defined
#endif
#ifndef ISP_PAGE_SIZE
#    error ISP_PAGE_SIZE must be defined
#endif
#ifndef XRAM_SIZE
#    error XRAM_SIZE must be defined
#endif

/**
 * Reset lands at 0x0000, so the application cannot own its own reset vector:
 * the erase command puts `LJMP BOOT_BASE` there and the bootloader keeps it,
 * skipping those three bytes on every page write. The application's own vector
 * is rebuilt as `LJMP <entry>` at APP_VECTOR_ADDR, and its presence is what
 * marks the application bootable.
 *
 * APP_VECTOR_ADDR is `firmware_size - 5`, where sinowisp puts it.
 */
#define BOOT_BASE       ((uint16_t)FLASH_FW_SIZE)
#define APP_VECTOR_ADDR ((uint16_t)(FLASH_FW_SIZE - 5u))

#define BOOT_BASE_ASM       (FLASH_FW_SIZE)
#define APP_VECTOR_ADDR_ASM (FLASH_FW_SIZE - 5)

#define LJMP_OPCODE 0x02u

// the entry stub writes these from assembly, so each one is its own macro
#define BOOT_REQUEST_MAGIC_0 0x53
#define BOOT_REQUEST_MAGIC_1 0x4d
#define BOOT_REQUEST_MAGIC_2 0x4b
#define BOOT_REQUEST_MAGIC_3 0xa5

#define BOOT_REQUEST_LEN      4u
#define BOOT_REQUEST_ADDR     ((uint16_t)(XRAM_SIZE - BOOT_REQUEST_LEN))
#define BOOT_REQUEST_ADDR_ASM (XRAM_SIZE - 4)

#define BOOT_REQUEST_MAGIC {BOOT_REQUEST_MAGIC_0, BOOT_REQUEST_MAGIC_1, BOOT_REQUEST_MAGIC_2, BOOT_REQUEST_MAGIC_3}

uint8_t boot_flash_byte(uint16_t addr);

/** True when an application reset vector has been committed at APP_VECTOR_ADDR. */
bool boot_app_enabled(void);

/** True when the application jumped to ISP_ENTRY asking for ISP mode; clears the request. */
bool boot_isp_requested(void);

void boot_enter_app(void) __naked;

/*
 * Supplied by the firmware that builds the bootloader. boot_board_init() runs
 * once after the clock and peripherals are up, to put the board's pins in a
 * safe state. boot_entry_held() then decides, on a cold start, whether to stay
 * in ISP mode rather than run a valid application: a key chord, a jumper, or
 * just `return false`.
 */
void boot_board_init(void);
bool boot_entry_held(void);
