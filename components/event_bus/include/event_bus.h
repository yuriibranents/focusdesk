/*
 * event_bus — дві черги FreeRTOS: події (q_events) і команди UI (q_ui) (B2).
 * Заголовок без залежностей від FreeRTOS: типи лише з focusdesk_common.h.
 *
 * Політика переповнення: подія НЕ блокує відправника довше за timeout_ms;
 * якщо черга повна, повертається FD_ERR_QUEUE_FULL, лічильник відкинутих
 * зростає, нова подія втрачається (стара лишається в черзі).
 * event_t.ts_us == 0 -> шина сама ставить поточний час (esp_timer).
 */
#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include "focusdesk_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EB_WAIT_FOREVER  UINT32_MAX

/* Створює обидві черги. FD_ERR_STATE, якщо вже створено; FD_ERR_NO_MEM. */
fd_err_t event_bus_init(void);
/* Видаляє черги (для тестів і перезапуску). Задачі мають бути зупинені. */
void     event_bus_deinit(void);

fd_err_t event_bus_post(const event_t *e, uint32_t timeout_ms);
fd_err_t event_bus_receive(event_t *e, uint32_t timeout_ms);   /* FD_ERR_TIMEOUT */

fd_err_t event_bus_post_ui(const ui_cmd_t *c, uint32_t timeout_ms);
fd_err_t event_bus_receive_ui(ui_cmd_t *c, uint32_t timeout_ms);

/* Скільки повідомлень відкинуто через повну чергу від init. */
uint32_t event_bus_dropped_events(void);
uint32_t event_bus_dropped_ui(void);

#ifdef __cplusplus
}
#endif
#endif /* EVENT_BUS_H */
