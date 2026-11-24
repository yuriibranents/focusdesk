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

static const char *TAG = "app";

#ifndef FD_SIM_PRESENCE
#define FD_SIM_PRESENCE 1
#endif

/* Симуляція (до роботи C/D): вигаданий UTC і демо-розклад з однією задачею. */
#define FD_SIM_UTC_BASE 1800000000LL

static fd_hb_t          s_hb;
static config_t         s_cfg;
static task_scheduler_t s_ts;       /* ~1,4 КБ: static, а не на стеку задачі */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Unix-час UTC або -1, якщо невідомий.
 * TODO(C): після SNTP повертати справжній UTC (time(NULL)). */
static fd_unix_t utc_now(void)
{
#if FD_SIM_PRESENCE
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

static void emit(const event_t *out)
{
    ESP_LOGI(TAG, "logic out: type=%d id=%d value=%d",
             (int)out->type, (int)out->id, (int)out->value);
    /* TODO(B7): арбітр перетворює вихідні події на ui_cmd_t і шле в q_ui */
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
}

static void handle_tick(uint32_t dt_s, sitting_monitor_t *sm,
                        focus_session_t *fs)
{
    event_t out;
    fd_time_us_t now_us = esp_timer_get_time();

    if (sm_tick(sm, dt_s, now_us, &out)) emit(&out);
    if (fs_tick(fs, dt_s, now_us, &out)) emit(&out);
    if (ts_tick(&s_ts, dt_s, utc_now(), now_us, &out)) emit(&out);
}

static void logic_task(void *arg)
{
    (void)arg;
    sitting_monitor_t sm;
    focus_session_t   fs;
    sm_init(&sm, &s_cfg);
    fs_init(&fs, &s_cfg);
    ts_init(&s_ts, &s_cfg);
#if FD_SIM_PRESENCE
    /* Демо-розклад: задача через 30 с після старту (замінить MQTT від C/D). */
    static schedule_t demo;
    memset(&demo, 0, sizeof(demo));
    demo.schedule_version = 1;
    demo.task_count = 1;
    demo.tasks[0].id = 1;
    demo.tasks[0].time_utc = FD_SIM_UTC_BASE + 30;
    strncpy(demo.tasks[0].text, "Demo task", FD_TEXT_MAX - 1);
    (void)ts_set_schedule(&s_ts, &demo, esp_timer_get_time(), NULL);
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
/* Presence Task (заглушка до B3)                                      */
/* ------------------------------------------------------------------ */

static void presence_task(void *arg)
{
    (void)arg;
#if FD_SIM_PRESENCE
    ESP_LOGW(TAG, "presence: SIMULATED (driver B3 not ready yet)");
    uint32_t sec = 0;
#else
    ESP_LOGW(TAG, "presence: stub, no events");
#endif
    for (;;) {
        app_heartbeat(FD_HB_PRESENCE);
#if FD_SIM_PRESENCE
        uint32_t phase = sec % 40u;
        if (phase == 3u)  post_simple(EVT_PRESENCE_BACK, 0);
        if (phase == 23u) post_simple(EVT_PRESENCE_LOST, 0);
        /* Демо-задача: DUE на 30-й с, повтор на 90-й; кнопки «бачу» і «виконано». */
        if (sec == 100u)  post_simple(EVT_TASK_SEEN, 0);
        if (sec == 110u)  post_simple(EVT_TASK_DONE, 0);
        sec++;
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ------------------------------------------------------------------ */
/* UI Task (заглушка до роботи C)                                      */
/* ------------------------------------------------------------------ */

static void ui_task(void *arg)
{
    (void)arg;
    for (;;) {
        ui_cmd_t cmd;
        if (event_bus_receive_ui(&cmd, 500) == FD_OK) {
            ESP_LOGI(TAG, "ui cmd: screen=%d value=%d",
                     (int)cmd.screen, (int)cmd.value);
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
        spawn(presence_task, "presence", FD_STACK_PRESENCE, FD_PRIO_PRESENCE, FD_CORE_APP)  &&
        spawn(sync_task,     "sync",     FD_STACK_SYNC,     FD_PRIO_SYNC,     FD_CORE_SYNC) &&
        spawn(monitor_task,  "monitor",  FD_STACK_MONITOR,  FD_PRIO_MONITOR,  tskNO_AFFINITY);

    return ok ? FD_OK : FD_ERR_NO_MEM;
}
