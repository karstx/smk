#include "isp.h"
#include "boot.h"
#include "usb.h"
#include "flash.h"
#include "watchdog.h"
#include <stdint.h>

#define CMD_ENABLE_FIRMWARE 0x55u
#define CMD_INIT_READ       0x52u
#define CMD_INIT_WRITE      0x57u
#define CMD_ERASE           0x45u
#define CMD_REBOOT          0x5au
#define CMD_ISP_MODE        0x75u

#define XFER_READ_PAGE  0x72u
#define XFER_WRITE_PAGE 0x77u

typedef enum {
    PENDING_NONE,
    PENDING_ERASE,
    PENDING_REBOOT,
} pending_t;

static uint16_t  cursor;
static uint8_t   command[ISP_CMD_LENGTH];
static uint8_t   active_get;
static uint8_t   active_set;
static uint8_t   xfer_op;
static pending_t pending;

/*
 * The application's `LJMP <entry>` operand, carried from the page-0 write to
 * the enable-firmware command that commits it. It cannot go straight into
 * flash: sinowisp reads the whole firmware back and compares it before
 * enabling, and APP_VECTOR_ADDR has to still read blank at that point.
 */
static uint8_t app_vector[2];
static bool    app_vector_valid;

void isp_init(void)
{
    cursor           = 0;
    active_get       = 0;
    active_set       = 0;
    xfer_op          = 0;
    pending          = PENDING_NONE;
    app_vector_valid = false;
}

static uint8_t isp_read(uint16_t addr)
{
    // 0x0000 holds the jump into the bootloader, so the application's own vector reads from where it was moved
    if (addr == 1u || addr == 2u) {
        uint8_t half = (uint8_t)(addr - 1u);
        return app_vector_valid ? app_vector[half] : boot_flash_byte((uint16_t)(APP_VECTOR_ADDR + 1u + half));
    }

    return boot_flash_byte(addr);
}

static void isp_program(uint16_t addr, uint8_t value)
{
    if (addr >= BOOT_BASE) {
        return; // the bootloader is not writable through its own protocol
    }

    if (addr < 3u) {
        if (addr != 0u) {
            app_vector[addr - 1u] = value;
            app_vector_valid      = true;
        }
        return;
    }

    if (addr >= APP_VECTOR_ADDR) {
        return; // reserved for enable_firmware
    }

    flash_program_byte(FLASH_CODE, addr, value);
}

static void enable_firmware(void)
{
    if (app_vector_valid) {
        flash_program_byte(FLASH_CODE, (uint16_t)(APP_VECTOR_ADDR + 1u), app_vector[0]);
        flash_program_byte(FLASH_CODE, (uint16_t)(APP_VECTOR_ADDR + 2u), app_vector[1]);
    }

    flash_program_byte(FLASH_CODE, APP_VECTOR_ADDR, LJMP_OPCODE);
}

static void erase_firmware(void)
{
    /*
     * The first sector holds the reset vector, so it is erased and rewritten
     * before the rest: unplugged mid-erase, the board still comes back up in
     * the bootloader instead of running off the end of a blank sector.
     */
    flash_erase(FLASH_CODE, 0u);
    flash_program_byte(FLASH_CODE, 0u, LJMP_OPCODE);
    flash_program_byte(FLASH_CODE, 1u, (uint8_t)(BOOT_BASE >> 8));
    flash_program_byte(FLASH_CODE, 2u, (uint8_t)BOOT_BASE);

    for (uint16_t addr = FLASH_SECTOR_SIZE; addr < BOOT_BASE; addr += FLASH_SECTOR_SIZE) {
        watchdog_kick();
        flash_erase(FLASH_CODE, addr);
    }

    app_vector_valid = false;
}

static void run_command(void)
{
    uint16_t addr = (uint16_t)(command[2] | ((uint16_t)command[3] << 8));
    uint8_t  cmd  = command[1];

    if (cmd == CMD_INIT_READ || cmd == CMD_INIT_WRITE) {
        cursor = addr;
    } else if (cmd == CMD_ENABLE_FIRMWARE) {
        enable_firmware();
    } else if (cmd == CMD_ERASE) {
        pending = PENDING_ERASE;
    } else if (cmd == CMD_REBOOT) {
        pending = PENDING_REBOOT;
    }
    // CMD_ISP_MODE asks the application to come here, and anything else is not ours
}

void isp_task(void)
{
    pending_t action = pending;
    pending          = PENDING_NONE;

    switch (action) {
        case PENDING_ERASE:
            erase_firmware();
            break;

        case PENDING_REBOOT:
            boot_usb_stop();
            boot_enter_app();
            break;

        default:
            break;
    }
}

bool isp_get_report_begin(uint8_t report_id, uint16_t *length)
{
    if (report_id != ISP_REPORT_XFER) {
        return false;
    }

    *length    = ISP_XFER_LENGTH;
    active_get = report_id;
    return true;
}

uint8_t isp_get_report_byte(uint16_t index)
{
    if (index == 0u) {
        return active_get;
    }

    if (index == 1u) {
        return XFER_READ_PAGE;
    }

    return isp_read((uint16_t)(cursor + index - 2u));
}

void isp_get_report_end(void)
{
    cursor += ISP_PAGE_SIZE;
    active_get = 0;
}

bool isp_set_report_begin(uint8_t report_id, uint16_t length)
{
    if (report_id == ISP_REPORT_CMD && length == ISP_CMD_LENGTH) {
        active_set = report_id;
        return true;
    }

    if (report_id == ISP_REPORT_XFER && length == ISP_XFER_LENGTH) {
        active_set = report_id;
        xfer_op    = 0;
        return true;
    }

    return false;
}

void isp_set_report_byte(uint16_t index, uint8_t value)
{
    if (active_set == ISP_REPORT_CMD) {
        if (index < ISP_CMD_LENGTH) {
            command[index] = value;
        }
        return;
    }

    if (index == 0u) {
        return; // report ID
    }

    if (index == 1u) {
        xfer_op = value;
        return;
    }

    if (xfer_op == XFER_WRITE_PAGE) {
        isp_program((uint16_t)(cursor + index - 2u), value);
    }
}

void isp_set_report_end(void)
{
    if (active_set == ISP_REPORT_CMD) {
        run_command();
    } else if (active_set == ISP_REPORT_XFER) {
        cursor += ISP_PAGE_SIZE;
    }

    active_set = 0;
}
