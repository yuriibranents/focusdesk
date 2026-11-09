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
 *   - відсутність < break_reset_min (10 хв) НЕ скидає лічильник і НЕ рахується
 *     як сидіння;
 *   - відсутність >= break_reset_min: перерва зарахована (EVT_BREAK_DONE),
 *     лічильник = 0, стан ABSENT;
 *   - EVT_BREAK_DUE видається один раз при досягненні порогу;
 *   - якщо людина відійшла з BREAK_DUE менше ніж на break_reset_min і
 *     повернулась, перерва не зарахована: повертаємось у BREAK_DUE без
 *     повторної події;
 *   - "Повернись": якщо відсутність від її початку досягла absence_alert_min
 *     (20 хв) і перерва вже зарахована, один раз видається EVT_ABSENCE_ALERT.
 *     Скидається по PRESENCE_BACK, лічильник сидіння стартує з 0.
 *     (При старті пристрою в ABSENT сповіщення НЕ видається: відлік іде
 *     лише після відходу з SITTING / BREAK_DUE.)
 *
 * За один виклик sm_tick максимум одна подія: якщо великий dt перетинає
 * обидва пороги, EVT_BREAK_DONE видається зараз, EVT_ABSENCE_ALERT на
 * наступному тіку.
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
    uint32_t   away_s;          /* відсутність від її початку, секунди
                                   (рахується й у ABSENT після перерви) */
    bool       break_was_due;   /* AWAY почався зі стану BREAK_DUE */
    bool       tracking_absence;/* відсутність після відходу триває */
    bool       alert_sent;      /* EVT_ABSENCE_ALERT уже видано */
} sitting_monitor_t;

/* cfg == NULL → значення за замовчуванням (fd_config_default). */
void sm_init(sitting_monitor_t *sm, const config_t *cfg);

/* Реакція на PRESENCE_LOST / PRESENCE_BACK; інші події ігноруються. */
void sm_handle_event(sitting_monitor_t *sm, const event_t *in);

/* Виклик раз на тік (зазвичай dt_s = 1). Якщо true — у *out вихідна подія
 * (EVT_BREAK_DUE, EVT_BREAK_DONE або EVT_ABSENCE_ALERT), ts_us = now_us.
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
