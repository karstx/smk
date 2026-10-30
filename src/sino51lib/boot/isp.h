#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ISP_REPORT_CMD    0x05u
#define ISP_REPORT_XFER   0x06u
#define ISP_REPORT_STATUS 0x07u

/** Report ID plus the five command bytes. */
#define ISP_CMD_LENGTH 6u
/** Report ID, the read/write opcode, then one flash page. */
#define ISP_XFER_LENGTH ((uint16_t)(ISP_PAGE_SIZE + 2u))

void isp_init(void);

/** Runs whatever the last command deferred; call it from the main loop. */
void isp_task(void);
