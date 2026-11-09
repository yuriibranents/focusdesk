/*
 * focusdesk_common.h — контракт між ролями A, B, C, D.
 * ВАЖЛИВО: чистий C, жодних залежностей від ESP-IDF/FreeRTOS,
 * щоб автомати компілювались і тестувались на ПК (gcc).
 * Після замикання (кінець 1-го тижня) зміни лише через Pull Request
 * із погодженням усіх чотирьох учасників.
 * Усі числові значення — СТАРТОВІ, уточнюються експериментами (E1..E11).
 */
#ifndef FOCUSDESK_COMMON_H
#define FOCUSDESK_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FD_API_VERSION      2       /* збільшувати при зміні контракту */
#define FOCUSDESK_COMMON_VERSION FD_API_VERSION  /* сумісність зі старим app_main.c */

/* ---------- Ліміти ---------- */
#define FD_TEXT_MAX         32      /* довжина тексту, байт, включно з '\0' */
#define FD_MAX_GOALS        8
#define FD_MAX_TASKS        16

/* ---------- Час ----------
 * Усередині: мікросекунди від старту (esp_timer_get_time()).
 * У розкладі та MQTT: UTC, Unix time (секунди).
 * На екрані: місцевий час (зсув у task_t.tz_offset_min).           */
typedef int64_t fd_time_us_t;
typedef int64_t fd_unix_t;

/* ---------- Коди помилок ---------- */
typedef enum {
    FD_OK = 0,
    FD_ERR_INVALID_ARG,
    FD_ERR_NO_MEM,
    FD_ERR_TIMEOUT,
    FD_ERR_QUEUE_FULL,
    FD_ERR_STORAGE,
    FD_ERR_SENSOR,
    FD_ERR_NOT_FOUND,
    FD_ERR_STATE            /* недопустимий перехід автомата */
} fd_err_t;

/* ---------- Події (черга q_events) ---------- */
typedef enum {
    EVT_NONE = 0,
    /* presence */
    EVT_PRESENCE_LOST,
    EVT_PRESENCE_BACK,
    /* sitting_monitor */
    EVT_BREAK_DUE,
    EVT_BREAK_DONE,
    EVT_ABSENCE_ALERT,      /* "Повернись": відсутність >= absence_alert_min, один раз */
    /* focus_session */
    EVT_FOCUS_START,
    EVT_FOCUS_PAUSE,
    EVT_FOCUS_RESUME,
    EVT_FOCUS_DONE,
    EVT_FOCUS_SUSPENDED,
    /* task_scheduler */
    EVT_TASK_DUE,
    EVT_TASK_SEEN,
    EVT_TASK_DONE,
    /* система */
    EVT_SCHEDULE_UPDATED,
    EVT_SYSTEM_ERROR,
    EVT_TICK,               /* тік планувальника 1 с (опційно, для тестів) */
    EVT__COUNT
} event_type_t;

typedef struct {
    event_type_t type;
    fd_time_us_t ts_us;             /* мітка часу, мкс від старту */
    uint16_t     id;                /* id цілі або задачі (0 = немає) */
    int32_t      value;             /* хвилини/секунди/код помилки — за типом події */
    char         text[FD_TEXT_MAX]; /* короткий текст (опційно) */
} event_t;

/* ---------- Команди для UI (черга q_ui) ---------- */
typedef enum {
    SCREEN_IDLE = 0,        /* час, нейтральний стан */
    SCREEN_GOAL_LIST,       /* список цілей на сьогодні */
    SCREEN_FOCUS,           /* ціль + залишок часу */
    SCREEN_FOCUS_PAUSED,
    SCREEN_BREAK,           /* нагадування про перерву */
    SCREEN_TASK,            /* текст задачі */
    SCREEN_ERROR,
    SCREEN_ABSENCE_ALERT    /* "Повернись до роботи" */
} ui_screen_t;

typedef enum {
    RING_OFF = 0,
    RING_STATIC,            /* постійний колір */
    RING_PROGRESS,          /* прогрес сесії (value = 0..100) */
    RING_BLINK,             /* мигання */
    RING_PULSE,             /* плавне «дихання» */
    RING_BLINK_FAST,        /* швидке мигання: задача (TASK_DUE) */
    RING_BLINK_SLOW         /* повільне мигання: "Повернись" (ABSENCE_ALERT) */
} ring_mode_t;

typedef enum {
    SOUND_NONE = 0,
    SOUND_BEEP_SHORT,
    SOUND_BEEP_ALERT,       /* нагадування про задачу */
    SOUND_BEEP_DONE,        /* завершення сесії */
    SOUND_BEEP_FAST,        /* швидкий звук: задача */
    SOUND_BEEP_SLOW         /* повільний звук: "Повернись" */
} sound_t;

typedef struct {
    ui_screen_t screen;
    ring_mode_t ring_mode;
    uint8_t     r, g, b;    /* колір кільця */
    uint8_t     ring_value; /* 0..100, для RING_PROGRESS */
    sound_t     sound;
    int32_t     value;      /* залишок часу, с (для екрана) */
    char        text[FD_TEXT_MAX];
} ui_cmd_t;

/* ---------- Дані розкладу ---------- */
typedef struct {
    uint16_t id;
    char     title[FD_TEXT_MAX];
    uint16_t minutes;       /* тривалість цілі */
} goal_t;

typedef enum {
    TASK_SCHEDULED = 0,
    TASK_DUE,
    TASK_SEEN,
    TASK_DONE
} task_status_t;

typedef struct {
    uint16_t      id;
    fd_unix_t     time_utc;         /* час нагадування, UTC */
    int16_t       tz_offset_min;    /* зсув для показу на екрані */
    char          text[FD_TEXT_MAX];
    task_status_t status;
} task_t;

typedef struct {
    uint32_t schedule_version;
    uint8_t  goal_count;
    uint8_t  task_count;
    goal_t   goals[FD_MAX_GOALS];
    task_t   tasks[FD_MAX_TASKS];
} schedule_t;

/* ---------- Конфігурація (7.5 документа) ---------- */
typedef struct {
    uint32_t sit_threshold_min;         /* 60 (налаштовується 60..90) */
    uint32_t break_reset_min;           /* 10 — відсутність, що скидає лічильник */
    uint32_t absence_alert_min;         /* 20 — "Повернись": відсутність від її початку */
    uint32_t presence_lost_debounce_s;  /* 10 */
    uint32_t presence_back_debounce_s;  /* 2  */
    uint32_t escalation_1_min;          /* 2  — нагадування після BREAK_DUE */
    uint32_t escalation_2_min;          /* 5 */
    uint32_t suspend_after_min;         /* 10 — до стану SUSPENDED */
    uint32_t task_repeat_s;             /* 60 — повтор нагадування до «бачу» */
    uint32_t scheduler_tick_s;          /* 1 */
    uint32_t nvs_save_period_s;         /* 30 */
} config_t;

static inline config_t fd_config_default(void)
{
    config_t c = {
        .sit_threshold_min        = 60,
        .break_reset_min          = 10,
        .absence_alert_min        = 20,
        .presence_lost_debounce_s = 10,
        .presence_back_debounce_s = 2,
        .escalation_1_min         = 2,
        .escalation_2_min         = 5,
        .suspend_after_min        = 10,
        .task_repeat_s            = 60,
        .scheduler_tick_s         = 1,
        .nvs_save_period_s        = 30,
    };
    return c;
}

/* ---------- Черги й задачі FreeRTOS (7.2 документа) ---------- */
#define FD_Q_EVENTS_LEN     16
#define FD_Q_UI_LEN         8

#define FD_CORE_SYNC        0
#define FD_CORE_APP         1

#define FD_PRIO_PRESENCE    5
#define FD_PRIO_LOGIC       4
#define FD_PRIO_UI          3
#define FD_PRIO_SYNC        3
#define FD_PRIO_MONITOR     6

#define FD_STACK_PRESENCE   3072    /* байт; уточнити uxTaskGetStackHighWaterMark */
#define FD_STACK_LOGIC      4096
#define FD_STACK_UI         6144
#define FD_STACK_SYNC       6144
#define FD_STACK_MONITOR    2048

/* Heartbeat: кожна задача виставляє свій біт, Monitor перевіряє. */
#define FD_HB_PRESENCE      (1u << 0)
#define FD_HB_LOGIC         (1u << 1)
#define FD_HB_UI            (1u << 2)
#define FD_HB_SYNC          (1u << 3)
#define FD_HB_ALL           (FD_HB_PRESENCE | FD_HB_LOGIC | FD_HB_UI | FD_HB_SYNC)
#define FD_HB_TIMEOUT_MS    10000

#ifdef __cplusplus
}
#endif
#endif /* FOCUSDESK_COMMON_H */
