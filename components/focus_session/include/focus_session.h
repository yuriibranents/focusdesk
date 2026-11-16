/*
 * focus_session — автомат фокус-сесії (B5).
 * ЧИСТИЙ C: без ESP-IDF і FreeRTOS, тестується на ПК.
 *
 * Стани (розділ 7.4 документа):
 *   IDLE       сесії немає
 *   READY      ціль обрана, чекаємо кнопку
 *   RUNNING    іде відлік часу
 *   PAUSED     людини немає, відлік стоїть
 *   SUSPENDED  відсутність >= suspend_after_min (10 хв): сесія "призупинена"
 *   DONE       час вийшов (перехідний стан на один тік, потім IDLE)
 *
 * Переходи:
 *   IDLE      --fs_select_goal-->        READY
 *   READY     --EVT_FOCUS_START-->       RUNNING   (подія START)
 *   RUNNING   --EVT_PRESENCE_LOST-->     PAUSED    (подія PAUSE)
 *   RUNNING   --час вийшов-->            DONE      (подія DONE)
 *   PAUSED    --EVT_PRESENCE_BACK-->     RUNNING   (подія RESUME)
 *   PAUSED    --відсутність 10 хв-->     SUSPENDED (подія SUSPENDED)
 *   SUSPENDED --EVT_FOCUS_START-->       RUNNING   (подія RESUME)
 *   DONE      --наступний тік-->         IDLE
 *   будь-який --fs_cancel-->             IDLE      (без події)
 *
 * EVT_FOCUS_START у вхідних подіях надсилає UI Task при натисканні кнопки.
 * Натискання кнопки вважається поверненням людини, тому з SUSPENDED
 * достатньо кнопки, окремий PRESENCE_BACK не потрібен.
 *
 * Вихідні події (id = id цілі, text = назва цілі):
 *   START      value = тривалість цілі, хвилини
 *   PAUSE      value = залишок, секунди
 *   RESUME     value = залишок, секунди
 *   SUSPENDED  value = залишок, секунди
 *   DONE       value = відпрацьовано, хвилини (= тривалість цілі)
 * Час без людини за сесію: fs_paused_seconds() (для підсумку в бот, 8.3).
 *
 * За один виклик максимум одна вихідна подія.
 */
#ifndef FOCUS_SESSION_H
#define FOCUS_SESSION_H

#include "focusdesk_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FS_IDLE = 0,
    FS_READY,
    FS_RUNNING,
    FS_PAUSED,
    FS_SUSPENDED,
    FS_DONE
} fs_state_t;

typedef struct {
    config_t   cfg;
    fs_state_t state;
    goal_t     goal;
    uint32_t   total_s;       /* тривалість цілі, секунди */
    uint32_t   remaining_s;   /* залишок, секунди */
    uint32_t   paused_s;      /* час без людини за сесію (PAUSED + SUSPENDED) */
    uint32_t   away_s;        /* відсутність від останнього PRESENCE_LOST */
} focus_session_t;

/* cfg == NULL → значення за замовчуванням (fd_config_default). */
void fs_init(focus_session_t *fs, const config_t *cfg);

/* Вибір цілі (енкодер). Дозволено з IDLE, READY, DONE.
 * false: порожні аргументи, minutes == 0 або сесія вже йде. */
bool fs_select_goal(focus_session_t *fs, const goal_t *goal);

/* Скасування сесії. false, якщо вже IDLE. Подію не видає. */
bool fs_cancel(focus_session_t *fs);

/* Реакція на EVT_FOCUS_START, EVT_PRESENCE_LOST, EVT_PRESENCE_BACK;
 * інші події ігноруються. Якщо true, у *out вихідна подія з ts_us = in->ts_us. */
bool fs_handle_event(focus_session_t *fs, const event_t *in, event_t *out);

/* Виклик раз на тік (зазвичай dt_s = 1). Якщо true, у *out вихідна подія
 * (EVT_FOCUS_DONE або EVT_FOCUS_SUSPENDED), ts_us = now_us. */
bool fs_tick(focus_session_t *fs, uint32_t dt_s, fd_time_us_t now_us,
             event_t *out);

fs_state_t     fs_state(const focus_session_t *fs);
uint32_t       fs_remaining_seconds(const focus_session_t *fs);
uint32_t       fs_paused_seconds(const focus_session_t *fs);
const goal_t  *fs_goal(const focus_session_t *fs);
const char    *fs_state_name(fs_state_t s);

#ifdef __cplusplus
}
#endif
#endif /* FOCUS_SESSION_H */
