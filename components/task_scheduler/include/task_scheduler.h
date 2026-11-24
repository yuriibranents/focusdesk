/*
 * task_scheduler — задачі-нагадування (B6).
 * ЧИСТИЙ C: без ESP-IDF і FreeRTOS, тестується на ПК.
 *
 * Стани задачі (task_status_t, розділ 7.4 документа):
 *   SCHEDULED  чекає на час
 *   DUE        час настав: кільце мигає, звук, текст на екрані
 *   SEEN       користувач натиснув «бачу»: мигання зупинено, задача в списку
 *   DONE       «виконано»: задача закрита (зберігається в NVS — це робить B8)
 *
 * Переходи:
 *   SCHEDULED --time_utc <= now_utc (ts_tick)-->   DUE   (подія EVT_TASK_DUE)
 *   DUE       --повтор раз на task_repeat_s-->     DUE   (EVT_TASK_DUE, value+1)
 *   DUE       --EVT_TASK_SEEN-->                   SEEN  (EVT_TASK_SEEN)
 *   DUE/SEEN  --EVT_TASK_DONE-->                   DONE  (EVT_TASK_DONE)
 *   SCHEDULED --EVT_TASK_DONE з id (з бота /done)--> DONE
 *
 * Нагадування повторюється кожні task_repeat_s (60 с) ДО натискання «бачу»
 * (7.5 документа). Після «бачу» повторів немає.
 *
 * Час: now_utc — Unix-час (UTC). Якщо SNTP ще не синхронізований, Logic Task
 * передає now_utc < 0: нові задачі тоді не стають DUE, але вже DUE-задачі
 * продовжують повторювати нагадування (лічильник іде від dt_s).
 *
 * Вихідні події (id = id задачі, text = текст задачі):
 *   EVT_TASK_DUE   value = номер нагадування: 0 — перше, 1, 2 — повтори
 *   EVT_TASK_SEEN  value = 0
 *   EVT_TASK_DONE  value = 0
 *   EVT_SCHEDULE_UPDATED (ts_set_schedule)  value = schedule_version
 * За один виклик ts_tick — максимум одна подія: якщо кілька задач настали
 * одночасно, решта отримає подію на наступних тіках (по 1 с на задачу).
 * Порядок на тіку: нова DUE > відновлена після перезапуску DUE > повтор.
 *
 * Вхідні події для ts_handle_event: EVT_TASK_SEEN, EVT_TASK_DONE
 * (від UI Task при натисканні кнопки, або від бота через C/D).
 *   id != 0: саме ця задача.
 *   id == 0 (кнопка на пристрої): SEEN → перша DUE-задача;
 *            DONE → перша SEEN-задача, а якщо таких немає — перша DUE.
 *
 * Розклад: ts_set_schedule приймає лише НОВУ версію (schedule_version більша
 * за поточну); дубль або старішу версію відхиляє (FD_ERR_STATE). Статуси задач
 * із тим самим id і тим самим часом зберігаються; якщо час задачі змінили,
 * вона знову SCHEDULED (крім DONE: закрита лишається закритою).
 * Задачі, часу яких уже минув, стають DUE на найближчому тіку: пропущене
 * нагадування краще показати, ніж загубити (E5: «втрачених має бути 0»).
 */
#ifndef TASK_SCHEDULER_H
#define TASK_SCHEDULER_H

#include "focusdesk_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    config_t   cfg;
    schedule_t sched;                    /* поточний розклад (цілі + задачі) */
    bool       has_schedule;
    uint32_t   since_s[FD_MAX_TASKS];    /* секунд від останнього нагадування (DUE) */
    uint16_t   sent[FD_MAX_TASKS];       /* скільки нагадувань уже видано */
    bool       notify[FD_MAX_TASKS];     /* відновлена DUE: нагадати на найближчому тіку */
} task_scheduler_t;

/* cfg == NULL → значення за замовчуванням (fd_config_default). Розклад порожній.
 * Структура велика (~1,4 КБ): тримати як static, а не на стеку задачі. */
void ts_init(task_scheduler_t *ts, const config_t *cfg);

/* Прийняти розклад від бота (MQTT). Повертає:
 *   FD_OK               прийнято; якщо out != NULL, там EVT_SCHEDULE_UPDATED
 *   FD_ERR_INVALID_ARG  порожні аргументи; більше FD_MAX_GOALS цілей або
 *                       FD_MAX_TASKS задач; id задачі == 0 або дубль id
 *   FD_ERR_STATE        версія не новіша за поточну (дубль або застаріла) */
fd_err_t ts_set_schedule(task_scheduler_t *ts, const schedule_t *s,
                         fd_time_us_t now_us, event_t *out);

/* Відновлення зі збереженого стану (B8/NVS) після перезапуску: версія не
 * порівнюється, статуси беруться як є. DUE-задачі отримають нагадування на
 * найближчому тіку. FD_ERR_INVALID_ARG при тих самих помилках, що вище, або
 * недопустимому статусі. */
fd_err_t ts_restore(task_scheduler_t *ts, const schedule_t *saved);

/* Реакція на EVT_TASK_SEEN / EVT_TASK_DONE; інші події ігноруються.
 * Якщо true, у *out вихідна подія з ts_us = in->ts_us. */
bool ts_handle_event(task_scheduler_t *ts, const event_t *in, event_t *out);

/* Виклик раз на тік (зазвичай dt_s = 1). now_utc — Unix-час, або < 0, якщо
 * час невідомий. Якщо true, у *out подія EVT_TASK_DUE з ts_us = now_us. */
bool ts_tick(task_scheduler_t *ts, uint32_t dt_s, fd_unix_t now_utc,
             fd_time_us_t now_us, event_t *out);

const schedule_t *ts_schedule(const task_scheduler_t *ts);  /* для екрана і B8 */
uint32_t          ts_schedule_version(const task_scheduler_t *ts);  /* 0 = розкладу немає */
const task_t     *ts_find(const task_scheduler_t *ts, uint16_t id);
const task_t     *ts_active(const task_scheduler_t *ts);    /* перша DUE або NULL (для арбітра) */
uint8_t           ts_due_count(const task_scheduler_t *ts);

/* Хвилини від місцевої півночі (0..1439) для показу на екрані: час UTC + зсув. */
int32_t ts_local_minute_of_day(const task_t *t);

const char *ts_status_name(task_status_t s);

#ifdef __cplusplus
}
#endif
#endif /* TASK_SCHEDULER_H */
