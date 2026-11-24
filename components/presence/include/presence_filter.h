/*
 * presence_filter — дебаунс присутності й контроль датчика (B3).
 * ЧИСТИЙ C: без ESP-IDF, тестується на ПК.
 *
 * Вхід: «сира» присутність від кожного кадру радара (раз на ~100 мс).
 * Вихід: стабільні події PRESENCE_BACK / PRESENCE_LOST.
 *   BACK: ціль безперервно присутня не менше presence_back_debounce_s (2 с)
 *   LOST: ціль безперервно відсутня не менше presence_lost_debounce_s (10 с)
 * Стартовий стан: «немає» (події при старті немає; перша поява людини дає
 * BACK через 2 с).
 *
 * Fail-safe: якщо кадрів немає довше за timeout, датчик вважається
 * несправним: один раз видається EVT_SYSTEM_ERROR (value = FD_ERR_SENSOR),
 * відлік дебаунсу скидається, і жодного PRESENCE_LOST не буде, поки
 * кадри не повернуться (пристрій не нараховує зайвих перерв). Коли кадри
 * з'являються, дебаунс стартує заново.
 *
 * Час у мілісекундах (uint32, безпечне переповнення).
 */
#ifndef PRESENCE_FILTER_H
#define PRESENCE_FILTER_H

#include "focusdesk_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t lost_debounce_ms;
    uint32_t back_debounce_ms;
    uint32_t sensor_timeout_ms;
    bool     present;            /* стабільний стан */
    bool     sensor_ok;
    bool     opposite_active;    /* сира величина відрізняється від стабільної */
    uint32_t opposite_since_ms;
    uint32_t last_frame_ms;
} presence_filter_t;

/* cfg == NULL → значення за замовчуванням. now_ms — поточний час. */
void pf_init(presence_filter_t *pf, const config_t *cfg,
             uint32_t sensor_timeout_ms, uint32_t now_ms);

/* Новий кадр радара. true: у *out подія PRESENCE_BACK або PRESENCE_LOST. */
bool pf_sample(presence_filter_t *pf, bool raw_present, uint32_t now_ms,
               event_t *out);

/* Викликати регулярно (наприклад, при кожному обході циклу).
 * true: у *out подія EVT_SYSTEM_ERROR (датчик замовк), один раз. */
bool pf_tick(presence_filter_t *pf, uint32_t now_ms, event_t *out);

bool pf_present(const presence_filter_t *pf);
bool pf_sensor_ok(const presence_filter_t *pf);

#ifdef __cplusplus
}
#endif
#endif /* PRESENCE_FILTER_H */
