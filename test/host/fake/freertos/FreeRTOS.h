/* Мінімальна імітація FreeRTOS для host-тестів event_bus. НЕ для прошивки. */
#ifndef FAKE_FREERTOS_H
#define FAKE_FREERTOS_H
#include <stdint.h>
typedef int      BaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE  1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#endif
