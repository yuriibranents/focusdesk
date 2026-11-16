#ifndef FAKE_ESP_TIMER_H
#define FAKE_ESP_TIMER_H
#include <stdint.h>
static inline int64_t esp_timer_get_time(void) { return 123456; }
#endif
