#include "raw_hid.h"

#ifdef VIA_ENABLE

#    include "usb.h"
#    include "usbhw.h"
#    include "via.h"
#    include <string.h>

static __xdata uint8_t request[RAW_HID_REPORT_SIZE];
/* Shared between the main loop and USB completion/RX interrupts. */
static volatile bool   request_pending;
static volatile bool   response_pending;

void raw_hid_init(void)
{
    request_pending  = false;
    response_pending = false;
    SET_EP2_OUT_RDY;
}

void raw_hid_receive_irq(void)
{
    if (request_pending || response_pending) {
        return;
    }
    if (OEP2CNT != RAW_HID_REPORT_SIZE) {
        SET_EP2_OUT_RDY;
        return;
    }
    for (uint8_t i = 0; i < RAW_HID_REPORT_SIZE; i++) {
        request[i] = EP2_OUT_BUF[i];
    }
    request_pending = true;
}

void raw_hid_task(void)
{
    if (!request_pending || !usb_is_configured() || !usb_hw_ep2_in_free()) {
        return;
    }

    via_process_packet(request);
    /* Publish ownership BEFORE arming EP2. A completion can run as soon as
     * the hardware sees IN_RDY. Never overwrite its state after submission.
     * OUT stays unarmed until that completion, protecting request[] while
     * via_process_packet and the copy to the endpoint are in progress. */
    request_pending  = false;
    response_pending = true;
    usb_hw_ep2_in_send(request, RAW_HID_REPORT_SIZE);
}

void raw_hid_in_complete(void)
{
    /* Ignore unrelated/stale completion: it must not reopen OUT while a
     * request is still owned by the foreground processor. */
    if (response_pending) {
        response_pending = false;
        SET_EP2_OUT_RDY;
    }
}

#endif
