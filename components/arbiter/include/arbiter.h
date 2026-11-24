/*
 * arbiter — вибір індикації (B7).
 * ЧИСТИЙ C: без ESP-IDF і FreeRTOS, тестується на ПК.
 *
 * Арбітр бачить події трьох автоматів і присутності та вирішує, ЩО показати
 * на екрані, кільці й зумері (ui_cmd_t). Автомати про арбітра не знають.
 *
 * Пріоритети (вищий перекриває нижчий):
 *   1  TASK      є хоч одна задача DUE         екран TASK, червоне мигання (швидке), швидкий звук
 *   2  ABSENCE   "Повернись" (ABSENCE_ALERT)   екран ABSENCE_ALERT, повільне мигання, повільний звук
 *   3  BREAK     BREAK_DUE, перерва не зарахована  екран BREAK, колір змінено, текст про перерву
 *   4  FOCUS     фокус-сесія (іде / пауза / призупинена)  екран FOCUS або FOCUS_PAUSED
 *   5  DONE      банер "сесію завершено" ARB_DONE_BANNER_S секунд  сигнал завершення
 *   6  IDLE      нейтральний колір, час
 *
 * Нижчі шари НЕ губляться: арбітр зберігає їхній стан, і коли вищий зникає,
 * індикація повертається до нижчого (фокус-сесія далі рахує час під
 * нагадуванням; "Повернись" після "бачу" повертається, доки людина не
 * повернеться, тобто до PRESENCE_BACK).
 *
 * Коли шар вмикається, звук відтворюється один раз. Звук нижчого шару, який
 * не встигли показати (його перекрив вищий), відтворюється, коли він
 * вперше стає видимим. Повтор TASK_DUE (кожні 60 с) дає звук знову.
 *
 * Події на вході (arb_handle_event):
 *   EVT_TASK_DUE (id, text; повтор тим самим id) додає задачу в список DUE
 *   EVT_TASK_SEEN, EVT_TASK_DONE (id)            прибирають її зі списку DUE
 *   EVT_ABSENCE_ALERT / EVT_PRESENCE_BACK        вмикає / вимикає "Повернись"
 *   EVT_BREAK_DUE / EVT_BREAK_DONE               вмикає / вимикає шар перерви
 *   EVT_FOCUS_START (id, value = хвилини, text = назва)
 *   EVT_FOCUS_PAUSE / RESUME / SUSPENDED (value = залишок, с)
 *   EVT_FOCUS_DONE (text = назва)
 * Інші події ігноруються. EVT_SYSTEM_ERROR поки не обробляється.
 *
 * Для TASK, якщо DUE-задач кілька, показується найстарша; ui_cmd_t.value =
 * кількість DUE-задач. Для FOCUS: value = залишок секунд, ring_value = прогрес
 * 0..100, text = назва цілі.
 *
 * Розклад може змінитись без подій про окремі задачі: після прийому нового
 * розкладу Logic Task викликає arb_sync_tasks з актуальним списком задач.
 */
#ifndef ARBITER_H
#define ARBITER_H

#include "focusdesk_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Тривалість банера "сесію завершено". */
#define ARB_DONE_BANNER_S   10

/* Кольори кільця (R, G, B). Уточнюються разом із C після фото на макеті. */
#define ARB_RGB_TASK        255,   0,   0
#define ARB_RGB_ABSENCE     255,  90,   0
#define ARB_RGB_BREAK       255, 160,   0
#define ARB_RGB_FOCUS         0, 200,  80
#define ARB_RGB_PAUSED        0, 120, 255
#define ARB_RGB_SUSPENDED    60,  60,  90
#define ARB_RGB_DONE          0, 255, 100
#define ARB_RGB_IDLE          0,  30,  60

/* Тексти для екрана (UTF-8, не більше FD_TEXT_MAX - 1 байт). */
#define ARB_TEXT_ABSENCE    "Повернись!"
#define ARB_TEXT_BREAK      "Час на перерву"

typedef enum {
    ARB_LAYER_IDLE = 0,
    ARB_LAYER_DONE,
    ARB_LAYER_FOCUS,
    ARB_LAYER_BREAK,
    ARB_LAYER_ABSENCE,
    ARB_LAYER_TASK
} arb_layer_t;

typedef enum {
    ARB_FOCUS_NONE = 0,
    ARB_FOCUS_RUNNING,
    ARB_FOCUS_PAUSED,
    ARB_FOCUS_SUSPENDED
} arb_focus_t;

typedef struct {
    uint16_t id;
    char     text[FD_TEXT_MAX];
} arb_task_t;

typedef struct {
    /* TASK */
    arb_task_t due[FD_MAX_TASKS];       /* у порядку появи, перша — найстаріша */
    uint8_t    due_count;
    /* інші шари */
    bool       absence_alert;
    bool       break_due;
    /* FOCUS */
    arb_focus_t focus;
    uint16_t    focus_id;
    char        focus_title[FD_TEXT_MAX];
    uint32_t    focus_total_s;
    uint32_t    focus_remaining_s;
    /* DONE */
    uint32_t    done_left_s;
    uint16_t    done_id;
    char        done_title[FD_TEXT_MAX];
    /* звуки, які ще не відтворено */
    bool        snd_task, snd_absence, snd_break, snd_done;
    /* останній виданий вигляд */
    bool        has_last;
    uint32_t    last_ident;
    ui_cmd_t    last;
} arbiter_t;

/* Структура ~0,7 КБ: тримати як static, а не на стеку задачі. */
void arb_init(arbiter_t *a);

/* Реакція на подію. Якщо true, у *out нова команда для UI (її кладуть у q_ui). */
bool arb_handle_event(arbiter_t *a, const event_t *in, ui_cmd_t *out);

/* Раз на тік (зазвичай dt_s = 1): рахує час фокус-сесії й банера.
 * true лише коли екран треба оновити: іде відлік фокусу, який зараз
 * показується (раз на тік, без звуку), або банер закінчився. Коли нічого
 * не змінилось, не видає нічого. */
bool arb_tick(arbiter_t *a, uint32_t dt_s, ui_cmd_t *out);

/* Синхронізація списку DUE-задач із розкладом (після нового розкладу):
 * зайве прибирає, відсутнє додає (без звуку). true: вигляд змінився. */
bool arb_sync_tasks(arbiter_t *a, const task_t *tasks, uint8_t count,
                    ui_cmd_t *out);

/* Поточний вигляд без звуку (для UI Task при старті; початковий IDLE
 * арбітр окремо не видає). */
bool arb_current(const arbiter_t *a, ui_cmd_t *out);

arb_layer_t arb_layer(const arbiter_t *a);       /* що зараз показується */
const char *arb_layer_name(arb_layer_t l);

#ifdef __cplusplus
}
#endif
#endif /* ARBITER_H */
