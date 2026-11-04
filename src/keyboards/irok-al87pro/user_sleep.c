#include "user_sleep.h"

#ifdef SLEEP_ENABLE

user_sleep_mode_t user_sleep_supported(void)
{
    return USER_SLEEP_NONE;
}

void user_sleep_prepare(void) {}
void user_sleep_wake(void) {}

#endif
