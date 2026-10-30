#include "boot.h"
#include <stdbool.h>

void boot_board_init(void)
{
}

// no key or pin to hold: ISP mode only when the application asks for it, or when there is no application
bool boot_entry_held(void)
{
    return false;
}
