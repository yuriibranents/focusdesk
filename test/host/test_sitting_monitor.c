/* Host-тести sitting_monitor (без ESP-IDF). Збірка: див. README компонента. */
#include "sitting_monitor.h"
#include <stdio.h>
#include <string.h>

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define MIN(n) ((uint32_t)(n) * 60u)

typedef struct { int due; int done; } counts_t;

static counts_t run(sitting_monitor_t *sm, uint32_t secs)
{
    counts_t c = {0, 0};
    event_t out;
    for (uint32_t i = 0; i < secs; i++) {
        if (sm_tick(sm, 1, (fd_time_us_t)i * 1000000, &out)) {
            if (out.type == EVT_BREAK_DUE)  c.due++;
            if (out.type == EVT_BREAK_DONE) c.done++;
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
    counts_t c = run(&sm, MIN(5) - 1);
    CHECK(c.done == 0);
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
    counts_t c = run(&sm, MIN(5) - 1);
    CHECK(c.done == 0 && sm_state(&sm) == SM_AWAY);
    c = run(&sm, 1);
    CHECK(c.done == 1);                     /* рівно на 5-й хвилині */
    CHECK(sm_state(&sm) == SM_ABSENT && sm_sit_seconds(&sm) == 0);
    c = run(&sm, 100);
    CHECK(c.done == 0 && c.due == 0);
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
    run(&sm, MIN(2));
    send(&sm, EVT_PRESENCE_BACK);
    CHECK(sm_state(&sm) == SM_BREAK_DUE);   /* перерва не взята */
    c = run(&sm, 60);
    CHECK(c.due == 0);                      /* без повторної події */
}

static void test_break_due_long_absence(void)
{
    sitting_monitor_t sm; sm_init(&sm, NULL);
    send(&sm, EVT_PRESENCE_BACK);
    run(&sm, MIN(60));
    send(&sm, EVT_PRESENCE_LOST);
    counts_t c = run(&sm, MIN(5));
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
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
