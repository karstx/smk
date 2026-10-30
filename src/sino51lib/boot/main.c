#include "boot.h"
#include "isp.h"
#include "usb.h"
#include "clock.h"
#include "ldo.h"
#include "peripherals.h"
#include "watchdog.h"
#include "sfr.h"
#include <stdbool.h>
#include <stdint.h>

void main(void)
{
    // the application can jump here with any SFR page or flash window selected
#ifdef sfr_page_0
    sfr_page_0();
#endif
    FLASHCON = 0;

    watchdog_kick();

    const bool requested = boot_isp_requested();

    ldo_init();
    clock_init();
    peripherals_init();

    boot_board_init();

    // the bootloader polls USB so it never claims a vector; the application owns them all
    IEN0 = 0;
    IEN1 = 0;

    if (!requested && !boot_entry_held() && boot_app_enabled()) {
        boot_enter_app();
    }

    isp_init();
    boot_usb_start();

    for (;;) {
        watchdog_kick();

        boot_usb_poll();
        isp_task();
    }
}
