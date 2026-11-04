#include "dynamic_keymap.h"

#ifdef DYNAMIC_KEYMAP_ENABLE

#    include "flash.h"
#    include "kbdef.h"
#    include "layout.h"
#    include "tick.h"
#    include <string.h>

#    define DYNAMIC_KEYMAP_MAGIC0  0x56u
#    define DYNAMIC_KEYMAP_MAGIC1  0x49u
#    ifndef DYNAMIC_KEYMAP_STORAGE_VERSION
#        define DYNAMIC_KEYMAP_STORAGE_VERSION 0x01u
#    endif
#    define DYNAMIC_KEYMAP_VERSION DYNAMIC_KEYMAP_STORAGE_VERSION
#    define DYNAMIC_KEYMAP_HEADER  8u
#    define DYNAMIC_KEYMAP_COMMIT  0xA5u

#    define DYNAMIC_KEYMAP_PAYLOAD_PER_SECTOR (DYNAMIC_KEYMAP_SECTOR_SIZE - DYNAMIC_KEYMAP_HEADER)
#    define DYNAMIC_KEYMAP_REQUIRED_SECTORS   ((DYNAMIC_KEYMAP_BYTES + DYNAMIC_KEYMAP_PAYLOAD_PER_SECTOR - 1u) / DYNAMIC_KEYMAP_PAYLOAD_PER_SECTOR)

_Static_assert(DYNAMIC_KEYMAP_LAYER_COUNT > 0, "dynamic keymap needs at least one layer");
_Static_assert(DYNAMIC_KEYMAP_LAYER_COUNT <= 16, "dynamic keymap layer count exceeds VIA protocol limits");
_Static_assert(DYNAMIC_KEYMAP_REQUIRED_SECTORS <= DYNAMIC_KEYMAP_SECTOR_COUNT, "dynamic keymap does not fit in reserved flash sectors");
_Static_assert((DYNAMIC_KEYMAP_BASE & (DYNAMIC_KEYMAP_SECTOR_SIZE - 1u)) == 0, "dynamic keymap base must be sector aligned");
_Static_assert(DYNAMIC_KEYMAP_BASE + DYNAMIC_KEYMAP_SECTOR_COUNT * DYNAMIC_KEYMAP_SECTOR_SIZE <= FLASH_CFG_ADDR, "dynamic keymap overlaps settings or boot marker flash");

static __xdata uint8_t dynamic_keymap[DYNAMIC_KEYMAP_BYTES];
static __xdata uint8_t io_buffer[32];
static volatile bool     dirty;
static volatile uint16_t save_delay;

static uint16_t checksum(void)
{
    uint16_t sum = 0x4B4Du;
    for (uint16_t i = 0; i < DYNAMIC_KEYMAP_BYTES; i++) {
        sum = (uint16_t)((sum << 5) | (sum >> 11));
        sum ^= dynamic_keymap[i];
    }
    return sum;
}

static uint16_t sector_base(uint8_t sector)
{
    return (uint16_t)(DYNAMIC_KEYMAP_BASE + (uint16_t)sector * DYNAMIC_KEYMAP_SECTOR_SIZE);
}

static uint16_t sector_payload_size(uint8_t sector)
{
    uint16_t offset = (uint16_t)sector * DYNAMIC_KEYMAP_PAYLOAD_PER_SECTOR;
    uint16_t left   = DYNAMIC_KEYMAP_BYTES - offset;
    return left < DYNAMIC_KEYMAP_PAYLOAD_PER_SECTOR ? left : DYNAMIC_KEYMAP_PAYLOAD_PER_SECTOR;
}

static bool load_flash(void)
{
    uint16_t expected_checksum = 0;

    for (uint8_t sector = 0; sector < DYNAMIC_KEYMAP_REQUIRED_SECTORS; sector++) {
        uint16_t base = sector_base(sector);
        flash_read_into(FLASH_CODE, base, io_buffer, DYNAMIC_KEYMAP_HEADER);
        if (io_buffer[0] != DYNAMIC_KEYMAP_MAGIC0 || io_buffer[1] != DYNAMIC_KEYMAP_MAGIC1 ||
            io_buffer[2] != DYNAMIC_KEYMAP_VERSION || io_buffer[3] != sector ||
            io_buffer[4] != DYNAMIC_KEYMAP_REQUIRED_SECTORS || io_buffer[7] != DYNAMIC_KEYMAP_COMMIT) {
            return false;
        }

        uint16_t stored_checksum = (uint16_t)((uint16_t)io_buffer[5] << 8) | io_buffer[6];
        if (sector == 0) {
            expected_checksum = stored_checksum;
        } else if (stored_checksum != expected_checksum) {
            return false;
        }

        uint16_t payload = sector_payload_size(sector);
        uint16_t offset  = (uint16_t)sector * DYNAMIC_KEYMAP_PAYLOAD_PER_SECTOR;
        while (payload) {
            uint8_t chunk = payload > sizeof(io_buffer) ? sizeof(io_buffer) : (uint8_t)payload;
            flash_read_into(FLASH_CODE, (uint16_t)(base + DYNAMIC_KEYMAP_HEADER + (offset % DYNAMIC_KEYMAP_PAYLOAD_PER_SECTOR)), io_buffer, chunk);
            memcpy(&dynamic_keymap[offset], io_buffer, chunk);
            offset += chunk;
            payload -= chunk;
        }
    }

    return checksum() == expected_checksum;
}

static void save_flash(void)
{
    uint16_t sum = checksum();

    tick_pause();
    for (uint8_t sector = 0; sector < DYNAMIC_KEYMAP_REQUIRED_SECTORS; sector++) {
        uint16_t base = sector_base(sector);
        flash_erase(FLASH_CODE, base);

        io_buffer[0] = DYNAMIC_KEYMAP_MAGIC0;
        io_buffer[1] = DYNAMIC_KEYMAP_MAGIC1;
        io_buffer[2] = DYNAMIC_KEYMAP_VERSION;
        io_buffer[3] = sector;
        io_buffer[4] = DYNAMIC_KEYMAP_REQUIRED_SECTORS;
        io_buffer[5] = (uint8_t)(sum >> 8);
        io_buffer[6] = (uint8_t)sum;
        // Leave commit byte 7 erased and program it exactly once, last.
        flash_program_from(FLASH_CODE, base, io_buffer, DYNAMIC_KEYMAP_HEADER - 1u);

        uint16_t payload = sector_payload_size(sector);
        uint16_t offset  = (uint16_t)sector * DYNAMIC_KEYMAP_PAYLOAD_PER_SECTOR;
        while (payload) {
            uint8_t chunk = payload > sizeof(io_buffer) ? sizeof(io_buffer) : (uint8_t)payload;
            memcpy(io_buffer, &dynamic_keymap[offset], chunk);
            flash_program_from(FLASH_CODE, (uint16_t)(base + DYNAMIC_KEYMAP_HEADER + (offset % DYNAMIC_KEYMAP_PAYLOAD_PER_SECTOR)), io_buffer, chunk);
            offset += chunk;
            payload -= chunk;
        }

        io_buffer[0] = DYNAMIC_KEYMAP_COMMIT;
        flash_program_from(FLASH_CODE, (uint16_t)(base + 7u), io_buffer, 1);
    }
    tick_resume();

    dirty = false;
}

static uint16_t key_offset(uint8_t layer, uint8_t row, uint8_t col)
{
    return ((uint16_t)layer * MATRIX_ROWS * MATRIX_COLS + (uint16_t)row * MATRIX_COLS + col) * 2u;
}

void dynamic_keymap_reset(void)
{
    for (uint8_t layer = 0; layer < DYNAMIC_KEYMAP_LAYER_COUNT; layer++) {
        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            for (uint8_t col = 0; col < MATRIX_COLS; col++) {
                uint16_t offset  = key_offset(layer, row, col);
                uint16_t keycode = keymaps[layer][row][col];
                dynamic_keymap[offset]     = (uint8_t)(keycode >> 8);
                dynamic_keymap[offset + 1] = (uint8_t)keycode;
            }
        }
    }
    __critical
    {
        dirty      = true;
        save_delay = 1;
    }
}

void dynamic_keymap_init(void)
{
    dirty      = false;
    save_delay = 0;
    if (!load_flash()) {
        dynamic_keymap_reset();
        save_flash();
    }
}

void dynamic_keymap_tick(void)
{
    if (save_delay) {
        save_delay--;
    }
}

void dynamic_keymap_task(void)
{
    bool due;
    __critical
    {
        due = dirty && save_delay == 0;
    }
    if (due) {
        save_flash();
    }
}

uint8_t dynamic_keymap_layer_count(void)
{
    return DYNAMIC_KEYMAP_LAYER_COUNT;
}

uint16_t keymap_get_keycode(uint8_t layer, uint8_t row, uint8_t col)
{
    if (layer >= DYNAMIC_KEYMAP_LAYER_COUNT || row >= MATRIX_ROWS || col >= MATRIX_COLS) {
        return KC_NO;
    }
    uint16_t offset = key_offset(layer, row, col);
    return (uint16_t)((uint16_t)dynamic_keymap[offset] << 8) | dynamic_keymap[offset + 1];
}

bool dynamic_keymap_set_keycode(uint8_t layer, uint8_t row, uint8_t col, uint16_t keycode)
{
    if (layer >= DYNAMIC_KEYMAP_LAYER_COUNT || row >= MATRIX_ROWS || col >= MATRIX_COLS) {
        return false;
    }
    uint16_t offset = key_offset(layer, row, col);
    if (dynamic_keymap[offset] == (uint8_t)(keycode >> 8) && dynamic_keymap[offset + 1] == (uint8_t)keycode) {
        return true;
    }
    dynamic_keymap[offset]     = (uint8_t)(keycode >> 8);
    dynamic_keymap[offset + 1] = (uint8_t)keycode;
    __critical
    {
        dirty      = true;
        save_delay = 1024;
    }
    return true;
}

uint8_t dynamic_keymap_get_buffer(uint16_t offset, uint8_t size, __xdata uint8_t *data)
{
    if (offset >= DYNAMIC_KEYMAP_BYTES) {
        return 0;
    }
    if ((uint16_t)(offset + size) > DYNAMIC_KEYMAP_BYTES) {
        size = (uint8_t)(DYNAMIC_KEYMAP_BYTES - offset);
    }
    memcpy(data, &dynamic_keymap[offset], size);
    return size;
}

uint8_t dynamic_keymap_set_buffer(uint16_t offset, uint8_t size, const __xdata uint8_t *data)
{
    if (offset >= DYNAMIC_KEYMAP_BYTES) {
        return 0;
    }
    if ((uint16_t)(offset + size) > DYNAMIC_KEYMAP_BYTES) {
        size = (uint8_t)(DYNAMIC_KEYMAP_BYTES - offset);
    }
    memcpy(&dynamic_keymap[offset], data, size);
    __critical
    {
        dirty      = true;
        save_delay = 1024;
    }
    return size;
}

#endif
