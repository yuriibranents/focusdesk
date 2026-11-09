/* Host-тести sitting_monitor (без ESP-IDF). Збірка: див. README компонента. */
#include "sitting_monitor.h"
#include <stdio.h>
#include <string.h>

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define MIN(n) ((uint32_t)(n) * 60u)

typedef struct { int due; int done; int alert; } counts_t;

static counts_t run(sitting_monitor_t *sm, uint32_t secs)
{
    counts_t c = {0, 0, 0};
    event_t out;
    for (uint32_t i = 0; i < secs; i++) {
        if (sm_tick(sm, 1, (fd_time_us_t)i * 1000000, &out)) {
            if (out.type == EVT_BREAK_DUE)  c.due++;
            if (out.type == EVT_BREAK_DONE) c.done++;
            if (out.type == EVT_ABSENCE_ALERT) c.alert++;
        }
    }
    return c;
}

static void send(sitting_monitor_t *sm, event_type_t t)
{
    event_t e;
    memset(&e, 0, sizeof(e));
    e.type = t;
    sm_handle_event(sm, &e);
}

static void test_start_and_sit(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    CHECK(sm_state(&sm) == SM_ABSENT);
    counts_t c = run(&sm, 100);
    CHECK(c.due == 0 && sm_sit_seconds(&sm) == 0);
    send(&sm, EVT_PRESENCE_BACK);
    CHECK(sm_state(&sm) == SM_SITTING);
}

static void test_threshold_event_once(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    counts_t c = run(&sm, MIN(60) - 1);
    CHECK(c.due == 0 && sm_state(&sm) == SM_SITTING);
    c = run(&sm, 1);
    CHECK(c.due == 1 && sm_state(&sm) == SM_BREAK_DUE);
    c = run(&sm, 600);
    CHECK(c.due == 0);                      /* подія не повторюється */
}

static void test_short_absence_keeps_counter(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    run(&sm, MIN(30));
    send(&sm, EVT_PRESENCE_LOST);
    CHECK(sm_state(&sm) == SM_AWAY);
    counts_t c = run(&sm, MIN(10) - 1);
    CHECK(c.done == 0 && c.alert == 0);
    CHECK(sm_sit_seconds(&sm) == MIN(30));  /* відсутність не рахується */
    send(&sm, EVT_PRESENCE_BACK);
    CHECK(sm_state(&sm) == SM_SITTING);
    CHECK(sm_sit_seconds(&sm) == MIN(30));  /* лічильник НЕ скинуто */
    c = run(&sm, MIN(30) - 1);
    CHECK(c.due == 0);
    c = run(&sm, 1);
    CHECK(c.due == 1);                      /* 30 + 30 хв = поріг */
}

static void test_long_absence_resets(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    run(&sm, MIN(30));
    send(&sm, EVT_PRESENCE_LOST);
    counts_t c = run(&sm, MIN(10) - 1);
    CHECK(c.done == 0 && sm_state(&sm) == SM_AWAY);
    c = run(&sm, 1);
    CHECK(c.done == 1);                     /* рівно на 10-й хвилині */
    CHECK(sm_state(&sm) == SM_ABSENT && sm_sit_seconds(&sm) == 0);
    c = run(&sm, 100);
    CHECK(c.done == 0 && c.due == 0 && c.alert == 0);
    send(&sm, EVT_PRESENCE_BACK);
    CHECK(sm_state(&sm) == SM_SITTING && sm_sit_seconds(&sm) == 0);
    c = run(&sm, MIN(60) - 1);
    CHECK(c.due == 0);
}

static void test_break_due_short_return(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    counts_t c = run(&sm, MIN(60));
    CHECK(c.due == 1 && sm_state(&sm) == SM_BREAK_DUE);
    send(&sm, EVT_PRESENCE_LOST);
    run(&sm, MIN(9));
    send(&sm, EVT_PRESENCE_BACK);
    CHECK(sm_state(&sm) == SM_BREAK_DUE);   /* <10 хв: перерва не взята */
    c = run(&sm, 60);
    CHECK(c.due == 0);                      /* без повторної події */
}

static void test_break_due_long_absence(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    run(&sm, MIN(60));
    send(&sm, EVT_PRESENCE_LOST);
    counts_t c = run(&sm, MIN(10));
    CHECK(c.done == 1 && sm_state(&sm) == SM_ABSENT);
    send(&sm, EVT_PRESENCE_BACK);
    CHECK(sm_state(&sm) == SM_SITTING && sm_sit_seconds(&sm) == 0);
}

static void test_ignored_events(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_LOST);           /* в ABSENT */
    CHECK(sm_state(&sm) == SM_ABSENT);
    send(&sm, EVT_PRESENCE_BACK);
    run(&sm, 100);
    send(&sm, EVT_PRESENCE_BACK);           /* повторний BACK у SITTING */
    CHECK(sm_state(&sm) == SM_SITTING && sm_sit_seconds(&sm) == 100);
    send(&sm, EVT_TASK_DUE);                /* чужа подія */
    CHECK(sm_state(&sm) == SM_SITTING);
}

static void test_custom_threshold_and_big_dt(void)
{
    config_t cfg = fd_config_default();
    cfg.sit_threshold_min = 90;
    sitting_monitor_t sm; sm_init(&sm, &cfg);
    send(&sm, EVT_PRESENCE_BACK);
    counts_t c = run(&sm, MIN(90) - 1);
    CHECK(c.due == 0);
    c = run(&sm, 1);
    CHECK(c.due == 1);

    sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    event_t out;
    CHECK(sm_tick(&sm, 4000, 123, &out));   /* великий dt */
    CHECK(out.type == EVT_BREAK_DUE && out.ts_us == 123 && out.value == 66);
}


static void test_absence_alert_after_break(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    run(&sm, MIN(30));
    send(&sm, EVT_PRESENCE_LOST);
    counts_t c = run(&sm, MIN(10));
    CHECK(c.done == 1 && c.alert == 0);     /* перерва, але ще не 20 хв */
    c = run(&sm, MIN(10) - 1);
    CHECK(c.alert == 0);                    /* 19:59 від початку відсутності */
    c = run(&sm, 1);
    CHECK(c.alert == 1);                    /* рівно 20 хв від початку */
    CHECK(sm_state(&sm) == SM_ABSENT);
    c = run(&sm, MIN(30));
    CHECK(c.alert == 0 && c.done == 0);     /* один раз */
}

static void test_absence_alert_value_and_ts(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    run(&sm, MIN(5));
    send(&sm, EVT_PRESENCE_LOST);
    event_t out;
    CHECK(sm_tick(&sm, MIN(10), 1, &out) && out.type == EVT_BREAK_DONE);
    CHECK(sm_tick(&sm, MIN(10), 777, &out));
    CHECK(out.type == EVT_ABSENCE_ALERT && out.ts_us == 777 && out.value == 20);
}

static void test_return_after_alert_restarts_from_zero(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    run(&sm, MIN(30));
    send(&sm, EVT_PRESENCE_LOST);
    counts_t c = run(&sm, MIN(25));
    CHECK(c.done == 1 && c.alert == 1);
    send(&sm, EVT_PRESENCE_BACK);
    CHECK(sm_state(&sm) == SM_SITTING && sm_sit_seconds(&sm) == 0);
    c = run(&sm, MIN(60) - 1);
    CHECK(c.alert == 0 && c.due == 0);
    /* новий відхід: сповіщення знову можливе */
    send(&sm, EVT_PRESENCE_LOST);
    c = run(&sm, MIN(20));
    CHECK(c.done == 1 && c.alert == 1);
}

static void test_return_between_break_and_alert(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    run(&sm, MIN(30));
    send(&sm, EVT_PRESENCE_LOST);
    counts_t c = run(&sm, MIN(15));         /* перерва зарахована, alert ще ні */
    CHECK(c.done == 1 && c.alert == 0);
    send(&sm, EVT_PRESENCE_BACK);
    CHECK(sm_state(&sm) == SM_SITTING && sm_sit_seconds(&sm) == 0);
    c = run(&sm, MIN(30));
    CHECK(c.alert == 0);                    /* відлік скасовано */
}

static void test_no_alert_without_prior_sitting(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    counts_t c = run(&sm, MIN(120));        /* пристрій увімкнули, нікого немає */
    CHECK(c.alert == 0 && c.done == 0 && c.due == 0);
}

static void test_short_absence_no_alert(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    run(&sm, MIN(30));
    send(&sm, EVT_PRESENCE_LOST);
    counts_t c = run(&sm, MIN(9));
    CHECK(c.alert == 0 && c.done == 0);
    send(&sm, EVT_PRESENCE_BACK);
    c = run(&sm, MIN(60));
    CHECK(c.alert == 0);
}

static void test_alert_from_break_due(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    run(&sm, MIN(60));
    send(&sm, EVT_PRESENCE_LOST);
    counts_t c = run(&sm, MIN(20));
    CHECK(c.done == 1 && c.alert == 1);
}

static void test_big_dt_crossing_both_thresholds(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    run(&sm, MIN(5));
    send(&sm, EVT_PRESENCE_LOST);
    event_t out;
    CHECK(sm_tick(&sm, MIN(25), 1, &out));  /* перетинає 10 і 20 хв */
    CHECK(out.type == EVT_BREAK_DONE);      /* одна подія за виклик */
    CHECK(sm_tick(&sm, 1, 2, &out));
    CHECK(out.type == EVT_ABSENCE_ALERT);   /* alert — на наступному тіку */
    CHECK(!sm_tick(&sm, 1, 3, &out));
}

static void test_custom_alert_threshold(void)
{
    config_t cfg = fd_config_default();
    cfg.absence_alert_min = 30;
    sitting_monitor_t sm; sm_init(&sm, &cfg);
    send(&sm, EVT_PRESENCE_BACK);
    run(&sm, MIN(5));
    send(&sm, EVT_PRESENCE_LOST);
    counts_t c = run(&sm, MIN(30) - 1);
    CHECK(c.alert == 0);
    c = run(&sm, 1);
    CHECK(c.alert == 1);
}

static void test_defaults(void)
{
    config_t cfg = fd_config_default();
    CHECK(cfg.break_reset_min == 10);
    CHECK(cfg.absence_alert_min == 20);
    CHECK(cfg.sit_threshold_min == 60);
}

int main(void)
{
    test_start_and_sit();
    test_threshold_event_once();
    test_short_absence_keeps_counter();
    test_long_absence_resets();
    test_break_due_short_return();
    test_break_due_long_absence();
    test_ignored_events();
    test_custom_threshold_and_big_dt();
    test_absence_alert_after_break();
    test_absence_alert_value_and_ts();
    test_return_after_alert_restarts_from_zero();
    test_return_between_break_and_alert();
    test_no_alert_without_prior_sitting();
    test_short_absence_no_alert();
    test_alert_from_break_due();
    test_big_dt_crossing_both_thresholds();
    test_custom_alert_threshold();
    test_defaults();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
