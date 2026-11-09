/*
 * sitting_monitor — автомат контролю безперервного сидіння (B4).
 * ЧИСТИЙ C: без ESP-IDF і FreeRTOS, тестується на ПК.
 *
 * Стани (розділ 7.4 документа):
 *   ABSENT     людини немає, лічильник = 0
 *   SITTING    лічильник безперервного сидіння росте
 *   AWAY       людина відійшла; рахуємо тривалість відсутності
 *   BREAK_DUE  поріг досягнуто, подію BREAK_DUE вже видано
 *
 * Правила:
 *   - відсутність < break_reset_min НЕ скидає лічильник і НЕ рахується як сидіння;
 *   - відсутність >= break_reset_min: перерва зарахована (EVT_BREAK_DONE),
 *     лічильник = 0, стан ABSENT;
 *   - EVT_BREAK_DUE видається один раз при досягненні порогу;
 *   - якщо людина відійшла з BREAK_DUE ненадовго й повернулась,
 *     перерва не взята: повертаємось у BREAK_DUE без повторної події.
 *
 * Не реалізовано (Should, F13): ескалація нагадувань.
 */
#ifndef SITTING_MONITOR_H
#define SITTING_MONITOR_H

#include "focusdesk_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SM_ABSENT = 0,
    SM_SITTING,
    SM_AWAY,
    SM_BREAK_DUE
} sm_state_t;

typedef struct {
    config_t   cfg;
    sm_state_t state;
    uint32_t   sit_s;           /* безперервне сидіння, секунди */
    uint32_t   away_s;          /* поточна відсутність, секунди */
    bool       break_was_due;   /* AWAY почався зі стану BREAK_DUE */
} sitting_monitor_t;

/* cfg == NULL → значення за замовчуванням (fd_config_default). */
void sm_init(sitting_monitor_t *sm, const config_t *cfg);

/* Реакція на PRESENCE_LOST / PRESENCE_BACK; інші події ігноруються. */
void sm_handle_event(sitting_monitor_t *sm, const event_t *in);

/* Виклик раз на тік (зазвичай dt_s = 1). Якщо true — у *out вихідна подія
 * (EVT_BREAK_DUE або EVT_BREAK_DONE), ts_us = now_us.
 * За один виклик максимум одна подія. */
bool sm_tick(sitting_monitor_t *sm, uint32_t dt_s, fd_time_us_t now_us,
             event_t *out);

sm_state_t  sm_state(const sitting_monitor_t *sm);
uint32_t    sm_sit_seconds(const sitting_monitor_t *sm);
const char *sm_state_name(sm_state_t s);

#ifdef __cplusplus
}
#endif
#endif /* SITTING_MONITOR_H */
