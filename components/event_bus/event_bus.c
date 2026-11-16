#include "event_bus.h"

#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "event_bus";

static QueueHandle_t   s_events;
static QueueHandle_t   s_ui;
static _Atomic uint32_t s_drop_events;
static _Atomic uint32_t s_drop_ui;

static TickType_t to_ticks(uint32_t timeout_ms)
{
    return (timeout_ms == EB_WAIT_FOREVER) ? portMAX_DELAY
                                           : pdMS_TO_TICKS(timeout_ms);
}

fd_err_t event_bus_init(void)
{
    if (s_events || s_ui) return FD_ERR_STATE;

    s_events = xQueueCreate(FD_Q_EVENTS_LEN, sizeof(event_t));
    s_ui     = xQueueCreate(FD_Q_UI_LEN, sizeof(ui_cmd_t));
    if (!s_events || !s_ui) {
        event_bus_deinit();
        return FD_ERR_NO_MEM;
    }
    atomic_store(&s_drop_events, 0);
    atomic_store(&s_drop_ui, 0);
    return FD_OK;
}

void event_bus_deinit(void)
{
    if (s_events) { vQueueDelete(s_events); s_events = NULL; }
    if (s_ui)     { vQueueDelete(s_ui);     s_ui = NULL; }
}

fd_err_t event_bus_post(const event_t *e, uint32_t timeout_ms)
{
    if (!e) return FD_ERR_INVALID_ARG;
    if (!s_events) return FD_ERR_STATE;

    event_t copy = *e;
    if (copy.ts_us == 0) copy.ts_us = esp_timer_get_time();

    if (xQueueSend(s_events, &copy, to_ticks(timeout_ms)) != pdTRUE) {
        atomic_fetch_add(&s_drop_events, 1);
        ESP_LOGW(TAG, "events queue full, dropped type=%d", (int)e->type);
        return FD_ERR_QUEUE_FULL;
    }
    return FD_OK;
}

fd_err_t event_bus_receive(event_t *e, uint32_t timeout_ms)
{
    if (!e) return FD_ERR_INVALID_ARG;
    if (!s_events) return FD_ERR_STATE;
    return xQueueReceive(s_events, e, to_ticks(timeout_ms)) == pdTRUE
               ? FD_OK : FD_ERR_TIMEOUT;
}

fd_err_t event_bus_post_ui(const ui_cmd_t *c, uint32_t timeout_ms)
{
    if (!c) return FD_ERR_INVALID_ARG;
    if (!s_ui) return FD_ERR_STATE;

    if (xQueueSend(s_ui, c, to_ticks(timeout_ms)) != pdTRUE) {
        atomic_fetch_add(&s_drop_ui, 1);
        ESP_LOGW(TAG, "ui queue full, dropped screen=%d", (int)c->screen);
        return FD_ERR_QUEUE_FULL;
    }
    return FD_OK;
}

fd_err_t event_bus_receive_ui(ui_cmd_t *c, uint32_t timeout_ms)
{
    if (!c) return FD_ERR_INVALID_ARG;
    if (!s_ui) return FD_ERR_STATE;
    return xQueueReceive(s_ui, c, to_ticks(timeout_ms)) == pdTRUE
               ? FD_OK : FD_ERR_TIMEOUT;
}

uint32_t event_bus_dropped_events(void) { return atomic_load(&s_drop_events); }
uint32_t event_bus_dropped_ui(void)     { return atomic_load(&s_drop_ui); }
