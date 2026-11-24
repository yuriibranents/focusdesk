#include "app_tasks.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "event_bus.h"
#include "fd_heartbeat.h"
#include "sitting_monitor.h"
#include "focus_session.h"
#include "task_scheduler.h"
#include "arbiter.h"
#include "ld2410_parser.h"
#include "presence_filter.h"
#include "presence_hw.h"

static const char *TAG = "app";

/* FD_SIM_PRESENCE: 1 = вигадана присутність замість радара (для столу без радара).
 * FD_SIM_DEMO:     1 = вигаданий UTC, демо-розклад і кнопки «бачу»/«виконано»
 *                  (до роботи C/D). */
#ifndef FD_SIM_PRESENCE
#define FD_SIM_PRESENCE 0
#endif
#ifndef FD_SIM_DEMO
#define FD_SIM_DEMO 1
#endif

#define FD_SIM_UTC_BASE 1800000000LL

static fd_hb_t          s_hb;
static config_t         s_cfg;
static task_scheduler_t s_ts;       /* ~1,4 КБ: static, а не на стеку задачі */
static arbiter_t        s_arb;      /* ~0,7 КБ: теж static */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Unix-час UTC або -1, якщо невідомий.
 * TODO(C): після SNTP повертати справжній UTC (time(NULL)). */
static fd_unix_t utc_now(void)
{
#if FD_SIM_DEMO
    return FD_SIM_UTC_BASE + (fd_unix_t)(esp_timer_get_time() / 1000000);
#else
    return -1;
#endif
}

void app_heartbeat(uint32_t bits)
{
    fd_hb_beat(&s_hb, bits, now_ms());
}

static void post_simple(event_type_t type, int32_t value)
{
    event_t e;
    memset(&e, 0, sizeof(e));
    e.type  = type;
    e.value = value;
    (void)event_bus_post(&e, 0);        /* не блокуємо; при переповненні подія втрачається */
}

/* ------------------------------------------------------------------ */
/* Logic Task                                                          */
/* ------------------------------------------------------------------ */

static void post_ui_cmd(const ui_cmd_t *cmd)
{
    (void)event_bus_post_ui(cmd, 0);    /* не блокуємо; при переповненні команда втрачається */
}

/* Вихідна подія автомата: лог і команда для UI від арбітра. */
static void emit(const event_t *out)
{
    ui_cmd_t cmd;
    ESP_LOGI(TAG, "logic out: type=%d id=%d value=%d",
             (int)out->type, (int)out->id, (int)out->value);
    if (arb_handle_event(&s_arb, out, &cmd)) post_ui_cmd(&cmd);
}

/* Єдина точка прийому розкладу: C/D викличуть її з Sync Task. Після нового
 * розкладу арбітр синхронізує список DUE-задач. */
__attribute__((unused))
static fd_err_t apply_schedule(const schedule_t *s)
{
    event_t ev;
    fd_err_t err = ts_set_schedule(&s_ts, s, esp_timer_get_time(), &ev);
    if (err == FD_OK) {
        ui_cmd_t cmd;
        const schedule_t *cur = ts_schedule(&s_ts);
        if (arb_sync_tasks(&s_arb, cur->tasks, cur->task_count, &cmd))
            post_ui_cmd(&cmd);
        emit(&ev);
    }
    return err;
}

static void handle_input(const event_t *in, sitting_monitor_t *sm,
                         focus_session_t *fs)
{
    event_t out;
    ESP_LOGI(TAG, "logic in: type=%d id=%d value=%d",
             (int)in->type, (int)in->id, (int)in->value);

    sm_handle_event(sm, in);
    if (fs_handle_event(fs, in, &out)) emit(&out);
    if (ts_handle_event(&s_ts, in, &out)) emit(&out);

    /* "Повернись" знімається поверненням людини: це подія присутності,
     * а не вихід автомата, тому арбітр отримує її напряму. */
    if (in->type == EVT_PRESENCE_BACK) {
        ui_cmd_t cmd;
        if (arb_handle_event(&s_arb, in, &cmd)) post_ui_cmd(&cmd);
    }
}

static void handle_tick(uint32_t dt_s, sitting_monitor_t *sm,
                        focus_session_t *fs)
{
    event_t out;
    fd_time_us_t now_us = esp_timer_get_time();

    if (sm_tick(sm, dt_s, now_us, &out)) emit(&out);
    if (fs_tick(fs, dt_s, now_us, &out)) emit(&out);
    if (ts_tick(&s_ts, dt_s, utc_now(), now_us, &out)) emit(&out);

    ui_cmd_t cmd;
    if (arb_tick(&s_arb, dt_s, &cmd)) post_ui_cmd(&cmd);
}

static void logic_task(void *arg)
{
    (void)arg;
    sitting_monitor_t sm;
    focus_session_t   fs;
    sm_init(&sm, &s_cfg);
    fs_init(&fs, &s_cfg);
    ts_init(&s_ts, &s_cfg);
    arb_init(&s_arb);
#if FD_SIM_DEMO
    /* Демо-розклад: задача через 30 с після старту (замінить MQTT від C/D). */
    static schedule_t demo;
    memset(&demo, 0, sizeof(demo));
    demo.schedule_version = 1;
    demo.task_count = 1;
    demo.tasks[0].id = 1;
    demo.tasks[0].time_utc = FD_SIM_UTC_BASE + 30;
    strncpy(demo.tasks[0].text, "Demo task", FD_TEXT_MAX - 1);
    (void)apply_schedule(&demo);
#endif

    uint32_t last_tick_ms = now_ms();
    for (;;) {
        uint32_t elapsed = now_ms() - last_tick_ms;
        uint32_t wait_ms = (elapsed >= 1000u) ? 0u : (1000u - elapsed);
        if (wait_ms > 0u && wait_ms < 10u) wait_ms = 10u;   /* тік FreeRTOS 10 мс */

        event_t in;
        if (event_bus_receive(&in, wait_ms) == FD_OK) {
            handle_input(&in, &sm, &fs);
        }

        elapsed = now_ms() - last_tick_ms;
        if (elapsed >= 1000u) {
            uint32_t dt_s = elapsed / 1000u;
            last_tick_ms += dt_s * 1000u;
            handle_tick(dt_s, &sm, &fs);
        }
        app_heartbeat(FD_HB_LOGIC);
    }
}

/* ------------------------------------------------------------------ */
/* Presence Task: радар LD2410C -> парсер -> фільтр -> події           */
/* ------------------------------------------------------------------ */

#define RADAR_SENSOR_TIMEOUT_MS 3000u   /* стільки без кадрів = датчик мовчить */
#define RADAR_SWAP_AFTER_MS     5000u   /* без жодного кадру: міняємо RX/TX */

static ld2410_parser_t   s_parser;      /* ~0,2 КБ: static */
static presence_filter_t s_pf;

static void post_event(const event_t *e)
{
    (void)event_bus_post(e, 0);
}

#if FD_SIM_PRESENCE
static void presence_task(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "presence: SIMULATED (no radar)");
    uint32_t sec = 0;
    for (;;) {
        app_heartbeat(FD_HB_PRESENCE);
        uint32_t phase = sec % 40u;
        if (phase == 3u)  post_simple(EVT_PRESENCE_BACK, 0);
        if (phase == 23u) post_simple(EVT_PRESENCE_LOST, 0);
        sec++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#else
static void presence_task(void *arg)
{
    (void)arg;
    int rx = PRESENCE_PIN_B;            /* 21 = RX за схемою A */
    int tx = PRESENCE_PIN_A;            /* 18 = TX */

    ld2410_init(&s_parser);
    pf_init(&s_pf, &s_cfg, RADAR_SENSOR_TIMEOUT_MS, now_ms());

    if (presence_hw_init(rx, tx) != FD_OK) {
        ESP_LOGE(TAG, "presence: UART init failed");
        event_t e = { .type = EVT_SYSTEM_ERROR, .value = (int32_t)FD_ERR_SENSOR };
        post_event(&e);
        for (;;) { app_heartbeat(FD_HB_PRESENCE); vTaskDelay(pdMS_TO_TICKS(500)); }
    }
    ESP_LOGI(TAG, "presence: radar driver started (RX=GPIO%d TX=GPIO%d)", rx, tx);

    uint8_t         buf[64];
    ld2410_frame_t  fr = {0};
    uint32_t        last_log_ms  = now_ms();
    uint32_t        last_swap_ms = now_ms();

    for (;;) {
        int n = presence_hw_read(buf, sizeof buf, 100);
        uint32_t t = now_ms();
        event_t ev;

        for (int i = 0; i < n; i++) {
            if (ld2410_feed(&s_parser, buf[i], &fr)) {
                if (pf_sample(&s_pf, ld2410_present(&fr), t, &ev)) {
                    ESP_LOGI(TAG, "presence: %s",
                             ev.type == EVT_PRESENCE_BACK ? "BACK" : "LOST");
                    post_event(&ev);
                }
            }
        }
        if (pf_tick(&s_pf, t, &ev)) {
            ESP_LOGW(TAG, "presence: radar silent for %u ms", RADAR_SENSOR_TIMEOUT_MS);
            post_event(&ev);
        }

        /* Жодного коректного кадру з самого старту: можливо, RX/TX навпаки. */
        if (s_parser.frames_ok == 0 && (uint32_t)(t - last_swap_ms) >= RADAR_SWAP_AFTER_MS) {
            int tmp = rx; rx = tx; tx = tmp;
            ESP_LOGW(TAG, "presence: no frames, swapping pins -> RX=GPIO%d TX=GPIO%d", rx, tx);
            (void)presence_hw_init(rx, tx);
            ld2410_init(&s_parser);
            last_swap_ms = t;
        }

        if ((uint32_t)(t - last_log_ms) >= 1000u) {
            last_log_ms = t;
            ESP_LOGI(TAG, "radar: frames=%u bad=%u skipped=%u raw=%d state=%d det=%ucm mov=%ucm/%u still=%ucm/%u | stable=%s sensor=%s",
                     (unsigned)s_parser.frames_ok, (unsigned)s_parser.frames_bad,
                     (unsigned)s_parser.bytes_skipped,
                     (int)ld2410_present(&fr), (int)fr.target_state,
                     (unsigned)fr.detect_cm, (unsigned)fr.moving_cm, (unsigned)fr.moving_energy,
                     (unsigned)fr.still_cm, (unsigned)fr.still_energy,
                     pf_present(&s_pf) ? "PRESENT" : "ABSENT",
                     pf_sensor_ok(&s_pf) ? "ok" : "SILENT");
        }
        app_heartbeat(FD_HB_PRESENCE);
    }
}
#endif

#if FD_SIM_DEMO
/* Демо кнопок до роботи C: «бачу» на 100-й с, «виконано» на 110-й. */
static void demo_task(void *arg)
{
    (void)arg;
    for (uint32_t sec = 0;; sec++) {
        if (sec == 100u) post_simple(EVT_TASK_SEEN, 0);
        if (sec == 110u) post_simple(EVT_TASK_DONE, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

/* ------------------------------------------------------------------ */
/* UI Task (заглушка до роботи C)                                      */
/* ------------------------------------------------------------------ */

static void ui_task(void *arg)
{
    (void)arg;
    for (;;) {
        ui_cmd_t cmd;
        if (event_bus_receive_ui(&cmd, 500) == FD_OK) {
            ESP_LOGI(TAG, "ui cmd: screen=%d ring=%d sound=%d value=%d",
                     (int)cmd.screen, (int)cmd.ring_mode, (int)cmd.sound,
                     (int)cmd.value);
        }
        app_heartbeat(FD_HB_UI);
    }
}

/* ------------------------------------------------------------------ */
/* Sync Task (заглушка до роботи D)                                    */
/* ------------------------------------------------------------------ */

static void sync_task(void *arg)
{
    (void)arg;
    for (;;) {
        app_heartbeat(FD_HB_SYNC);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ------------------------------------------------------------------ */
/* Monitor Task: heartbeat і перезавантаження при зависанні            */
/* ------------------------------------------------------------------ */

static void monitor_task(void *arg)
{
    (void)arg;
    fd_wd_t wd;
    fd_wd_init(&wd, FD_WD_GRACE_MS);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t now   = now_ms();
        uint32_t stale = fd_hb_stale(&s_hb, now);

        switch (fd_wd_step(&wd, stale, now)) {
        case FD_WD_STALE_NEW:
            ESP_LOGE(TAG, "watchdog: tasks silent, mask=0x%x (1=presence 2=logic 4=ui 8=sync)",
                     (unsigned)stale);
            post_simple(EVT_SYSTEM_ERROR, (int32_t)stale);
            break;
        case FD_WD_RESTART:
            ESP_LOGE(TAG, "watchdog: still silent, mask=0x%x, restarting", (unsigned)stale);
            vTaskDelay(pdMS_TO_TICKS(100));        /* дати логу вийти */
            esp_restart();
            break;
        case FD_WD_STALE_WAIT:
        case FD_WD_OK:
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------ */

static bool spawn(TaskFunction_t fn, const char *name, uint32_t stack_bytes,
                  UBaseType_t prio, BaseType_t core)
{
    if (xTaskCreatePinnedToCore(fn, name, stack_bytes, NULL, prio, NULL, core) != pdPASS) {
        ESP_LOGE(TAG, "cannot create task %s", name);
        return false;
    }
    return true;
}

fd_err_t app_tasks_start(void)
{
    fd_err_t err = event_bus_init();
    if (err != FD_OK) return err;

    s_cfg = fd_config_default();
    fd_hb_init(&s_hb, FD_HB_TIMEOUT_MS, now_ms());

    bool ok =
        spawn(logic_task,    "logic",    FD_STACK_LOGIC,    FD_PRIO_LOGIC,    FD_CORE_APP)  &&
        spawn(ui_task,       "ui",       FD_STACK_UI,       FD_PRIO_UI,       FD_CORE_APP)  &&
        spawn(presence_task, "presence", 4096,              FD_PRIO_PRESENCE, FD_CORE_APP)  &&
        spawn(sync_task,     "sync",     FD_STACK_SYNC,     FD_PRIO_SYNC,     FD_CORE_SYNC) &&
        spawn(monitor_task,  "monitor",  FD_STACK_MONITOR,  FD_PRIO_MONITOR,  tskNO_AFFINITY);
#if FD_SIM_DEMO
    if (ok) ok = spawn(demo_task, "demo", 2048, 2, FD_CORE_APP);
#endif

    return ok ? FD_OK : FD_ERR_NO_MEM;
}
