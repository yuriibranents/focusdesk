#include "focusdesk_common.h"
#include <stdio.h>

int main(void) {
    config_t c = fd_config_default();
    printf("event_t=%zu ui_cmd_t=%zu schedule_t=%zu thr=%u ver=%d\n",
           sizeof(event_t), sizeof(ui_cmd_t), sizeof(schedule_t),
           (unsigned)c.sit_threshold_min, FOCUSDESK_COMMON_VERSION);
    return 0;
}
