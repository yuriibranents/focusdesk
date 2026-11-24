/* Host-тести task_scheduler (без ESP-IDF). */
#include "task_scheduler.h"
#include <stdio.h>
#include <string.h>

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define T0 1800000000LL          /* довільний Unix-час для тестів */

static schedule_t make_sched(uint32_t version, int n, const fd_unix_t *times)
{
    schedule_t s;
    memset(&s, 0, sizeof(s));
    s.schedule_version = version;
    s.task_count = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        s.tasks[i].id = (uint16_t)(10 + i);
        s.tasks[i].time_utc = times[i];
        snprintf(s.tasks[i].text, sizeof(s.tasks[i].text), "task %d", 10 + i);
    }
    return s;
}

static schedule_t one_task(uint32_t version, fd_unix_t when)
{
    return make_sched(version, 1, &when);
}

static void load(task_scheduler_t *ts, const config_t *cfg, const schedule_t *s)
{
    ts_init(ts, cfg);
    CHECK(ts_set_schedule(ts, s, 0, NULL) == FD_OK);
}

static bool send(task_scheduler_t *ts, event_type_t t, uint16_t id,
                 fd_time_us_t ts_us, event_t *out)
{
    event_t in;
    memset(&in, 0, sizeof(in));
    in.type = t;
    in.id = id;
    in.ts_us = ts_us;
    return ts_handle_event(ts, &in, out);
}

/* Крок у 1 с від now до now+secs-1; повертає кількість DUE-подій. */
static int run(task_scheduler_t *ts, fd_unix_t now, uint32_t secs)
{
    int dues = 0;
    event_t out;
    for (uint32_t i = 0; i < secs; i++)
        if (ts_tick(ts, 1, now + (fd_unix_t)i, (fd_time_us_t)i * 1000000, &out) &&
            out.type == EVT_TASK_DUE) dues++;
    return dues;
}

/* ------------------------------------------------------------------ */

static void test_empty_and_defaults(void)
{
    task_scheduler_t ts; event_t out;
    ts_init(&ts, NULL);
    CHECK(ts_schedule_version(&ts) == 0);
    CHECK(ts_active(&ts) == NULL);
    CHECK(ts_due_count(&ts) == 0);
    CHECK(!ts_tick(&ts, 1, T0, 0, &out));
    CHECK(ts.cfg.task_repeat_s == 60);
    CHECK(ts_schedule(&ts)->task_count == 0);
}

static void test_set_schedule_and_versions(void)
{
    task_scheduler_t ts; event_t out;
    schedule_t s = one_task(7, T0 + 100);
    s.tasks[0].status = TASK_DONE;          /* вхідний status ігнорується */

    ts_init(&ts, NULL);
    CHECK(ts_set_schedule(&ts, &s, 555, &out) == FD_OK);
    CHECK(out.type == EVT_SCHEDULE_UPDATED);
    CHECK(out.value == 7);
    CHECK(out.ts_us == 555);
    CHECK(ts_schedule_version(&ts) == 7);
    CHECK(ts_find(&ts, 10)->status == TASK_SCHEDULED);

    /* дубль і старіша версія відхиляються, розклад не змінюється */
    schedule_t same = one_task(7, T0 + 999);
    schedule_t old  = one_task(3, T0 + 999);
    CHECK(ts_set_schedule(&ts, &same, 0, NULL) == FD_ERR_STATE);
    CHECK(ts_set_schedule(&ts, &old, 0, NULL) == FD_ERR_STATE);
    CHECK(ts_find(&ts, 10)->time_utc == T0 + 100);
    CHECK(ts_schedule_version(&ts) == 7);

    /* нова версія приймається */
    schedule_t newer = one_task(8, T0 + 200);
    CHECK(ts_set_schedule(&ts, &newer, 0, &out) == FD_OK);
    CHECK(out.value == 8);
    CHECK(ts_find(&ts, 10)->time_utc == T0 + 200);
}

static void test_invalid_schedules(void)
{
    task_scheduler_t ts;
    schedule_t s;
    ts_init(&ts, NULL);

    CHECK(ts_set_schedule(NULL, &s, 0, NULL) == FD_ERR_INVALID_ARG);
    CHECK(ts_set_schedule(&ts, NULL, 0, NULL) == FD_ERR_INVALID_ARG);

    s = one_task(1, T0);
    s.task_count = FD_MAX_TASKS + 1;
    CHECK(ts_set_schedule(&ts, &s, 0, NULL) == FD_ERR_INVALID_ARG);

    s = one_task(1, T0);
    s.goal_count = FD_MAX_GOALS + 1;
    CHECK(ts_set_schedule(&ts, &s, 0, NULL) == FD_ERR_INVALID_ARG);

    s = one_task(1, T0);
    s.tasks[0].id = 0;
    CHECK(ts_set_schedule(&ts, &s, 0, NULL) == FD_ERR_INVALID_ARG);

    fd_unix_t two[2] = { T0, T0 + 5 };
    s = make_sched(1, 2, two);
    s.tasks[1].id = s.tasks[0].id;           /* дубль id */
    CHECK(ts_set_schedule(&ts, &s, 0, NULL) == FD_ERR_INVALID_ARG);

    CHECK(ts_schedule_version(&ts) == 0);    /* нічого не прийнято */

    /* максимальний розклад приймається */
    fd_unix_t many[FD_MAX_TASKS];
    for (int i = 0; i < FD_MAX_TASKS; i++) many[i] = T0 + i;
    s = make_sched(1, FD_MAX_TASKS, many);
    s.goal_count = FD_MAX_GOALS;
    CHECK(ts_set_schedule(&ts, &s, 0, NULL) == FD_OK);
    CHECK(ts_set_schedule(&ts, &ts.sched, 0, NULL) == FD_ERR_INVALID_ARG);
}

static void test_due_exactly_at_time(void)
{
    task_scheduler_t ts; event_t out;
    schedule_t s = one_task(1, T0 + 50);
    load(&ts, NULL, &s);

    CHECK(!ts_tick(&ts, 1, T0 + 49, 0, &out));
    CHECK(ts_find(&ts, 10)->status == TASK_SCHEDULED);

    CHECK(ts_tick(&ts, 1, T0 + 50, 4242, &out));
    CHECK(out.type == EVT_TASK_DUE);
    CHECK(out.id == 10);
    CHECK(out.value == 0);
    CHECK(out.ts_us == 4242);
    CHECK(strcmp(out.text, "task 10") == 0);
    CHECK(ts_find(&ts, 10)->status == TASK_DUE);
    CHECK(ts_active(&ts) == ts_find(&ts, 10));
    CHECK(ts_due_count(&ts) == 1);

    CHECK(!ts_tick(&ts, 1, T0 + 51, 0, &out));   /* не повторюється щосекунди */
}

static void test_latency_within_one_tick(void)
{
    /* E4: нагадування не пізніше ніж через 1 тік після запланованого часу. */
    for (int phase = 0; phase < 5; phase++) {
        task_scheduler_t ts; event_t out;
        schedule_t s = one_task(1, T0 + 100 + phase);
        load(&ts, NULL, &s);
        fd_unix_t fired = -1;
        for (fd_unix_t now = T0 + 90; now < T0 + 120; now++)
            if (ts_tick(&ts, 1, now, 0, &out) && fired < 0) fired = now;
        CHECK(fired == T0 + 100 + phase);
    }
}

static void test_repeat_until_seen(void)
{
    task_scheduler_t ts; event_t out;
    schedule_t s = one_task(1, T0);
    load(&ts, NULL, &s);

    CHECK(ts_tick(&ts, 1, T0, 0, &out));          /* t=0: перше */
    CHECK(out.value == 0);

    int dues = 0;
    fd_unix_t now = T0 + 1;
    for (int sec = 1; sec <= 59; sec++, now++)
        if (ts_tick(&ts, 1, now, 0, &out)) dues++;
    CHECK(dues == 0);                              /* 59 с тиші */

    CHECK(ts_tick(&ts, 1, now++, 0, &out));       /* 60 с: перший повтор */
    CHECK(out.type == EVT_TASK_DUE);
    CHECK(out.value == 1);

    for (int sec = 1; sec <= 59; sec++, now++)
        CHECK(!ts_tick(&ts, 1, now, 0, &out));
    CHECK(ts_tick(&ts, 1, now++, 0, &out));       /* ще через 60 с */
    CHECK(out.value == 2);

    CHECK(send(&ts, EVT_TASK_SEEN, 0, 9, &out));  /* «бачу» */
    CHECK(out.type == EVT_TASK_SEEN);
    CHECK(out.id == 10);
    CHECK(out.ts_us == 9);
    CHECK(strcmp(out.text, "task 10") == 0);
    CHECK(run(&ts, now, 600) == 0);                /* повторів більше немає */
    CHECK(ts_find(&ts, 10)->status == TASK_SEEN);
    CHECK(ts_active(&ts) == NULL);
}

static void test_custom_repeat(void)
{
    task_scheduler_t ts; config_t cfg = fd_config_default();
    schedule_t s = one_task(1, T0);
    cfg.task_repeat_s = 5;
    load(&ts, &cfg, &s);
    CHECK(run(&ts, T0, 21) == 5);                  /* t=0,5,10,15,20 */

    cfg.task_repeat_s = 0;                          /* повтор вимкнено */
    load(&ts, &cfg, &s);
    CHECK(run(&ts, T0, 500) == 1);
}

static void test_seen_rules(void)
{
    task_scheduler_t ts; event_t out;
    fd_unix_t when[3] = { T0 + 10, T0 + 10, T0 + 1000 };
    schedule_t s = make_sched(1, 3, when);
    load(&ts, NULL, &s);

    CHECK(!send(&ts, EVT_TASK_SEEN, 0, 0, &out));  /* ще немає DUE */
    CHECK(!send(&ts, EVT_TASK_SEEN, 10, 0, &out)); /* SCHEDULED не можна */

    run(&ts, T0 + 10, 3);                           /* обидві першого часу стали DUE */
    CHECK(ts_due_count(&ts) == 2);

    CHECK(!send(&ts, EVT_TASK_SEEN, 99, 0, &out)); /* невідомий id */
    CHECK(!send(&ts, EVT_TASK_SEEN, 12, 0, &out)); /* 12 ще SCHEDULED */

    CHECK(send(&ts, EVT_TASK_SEEN, 0, 0, &out));   /* id=0: перша DUE */
    CHECK(out.id == 10);
    CHECK(!send(&ts, EVT_TASK_SEEN, 10, 0, &out)); /* вдруге не можна */
    CHECK(ts_active(&ts) == ts_find(&ts, 11));      /* активна — наступна DUE */

    CHECK(send(&ts, EVT_TASK_SEEN, 11, 0, &out));  /* за id */
    CHECK(ts_active(&ts) == NULL);
    CHECK(ts_due_count(&ts) == 0);
}

static void test_done_rules(void)
{
    task_scheduler_t ts; event_t out;
    fd_unix_t when[3] = { T0, T0, T0 + 5000 };
    schedule_t s = make_sched(1, 3, when);
    load(&ts, NULL, &s);
    run(&ts, T0, 3);                                /* 10 і 11 — DUE */

    CHECK(send(&ts, EVT_TASK_SEEN, 11, 0, &out));  /* 11 — SEEN, 10 — DUE */

    /* id=0: спочатку SEEN-задача (11), а не DUE (10) */
    CHECK(send(&ts, EVT_TASK_DONE, 0, 77, &out));
    CHECK(out.type == EVT_TASK_DONE);
    CHECK(out.id == 11);
    CHECK(out.ts_us == 77);
    CHECK(ts_find(&ts, 11)->status == TASK_DONE);

    /* тепер SEEN немає: id=0 закриває першу DUE (10) прямо з DUE */
    CHECK(send(&ts, EVT_TASK_DONE, 0, 0, &out));
    CHECK(out.id == 10);
    CHECK(ts_find(&ts, 10)->status == TASK_DONE);

    CHECK(!send(&ts, EVT_TASK_DONE, 0, 0, &out));  /* SCHEDULED без id не чіпаємо */
    CHECK(!send(&ts, EVT_TASK_DONE, 10, 0, &out)); /* вже DONE */
    CHECK(ts_find(&ts, 12)->status == TASK_SCHEDULED);

    /* із бота (/done <id>) можна закрити й незапланований заздалегідь */
    CHECK(send(&ts, EVT_TASK_DONE, 12, 0, &out));
    CHECK(ts_find(&ts, 12)->status == TASK_DONE);
    CHECK(run(&ts, T0 + 6000, 300) == 0);          /* DONE більше не нагадує */
}

static void test_simultaneous_tasks(void)
{
    task_scheduler_t ts; event_t out;
    fd_unix_t when[3] = { T0, T0, T0 };
    schedule_t s = make_sched(1, 3, when);
    load(&ts, NULL, &s);

    /* максимум одна подія за тік, по порядку розкладу */
    CHECK(ts_tick(&ts, 1, T0, 0, &out)); CHECK(out.id == 10);
    CHECK(ts_tick(&ts, 1, T0 + 1, 0, &out)); CHECK(out.id == 11);
    CHECK(ts_tick(&ts, 1, T0 + 2, 0, &out)); CHECK(out.id == 12);
    CHECK(!ts_tick(&ts, 1, T0 + 3, 0, &out));
    CHECK(ts_due_count(&ts) == 3);

    /* повтори теж по одному на тік */
    int dues = 0;
    for (int i = 0; i < 80; i++)
        if (ts_tick(&ts, 1, T0 + 4 + i, 0, &out)) dues++;
    CHECK(dues == 3);
}

static void test_unknown_time(void)
{
    task_scheduler_t ts; event_t out;
    schedule_t s = one_task(1, T0);
    load(&ts, NULL, &s);

    /* SNTP немає: now_utc < 0 — задача не стає DUE */
    for (int i = 0; i < 200; i++)
        CHECK(!ts_tick(&ts, 1, -1, 0, &out));
    CHECK(ts_find(&ts, 10)->status == TASK_SCHEDULED);

    /* час з'явився, задача в минулому: спрацьовує на першому тіку */
    CHECK(ts_tick(&ts, 1, T0 + 100, 0, &out));
    CHECK(out.type == EVT_TASK_DUE);

    /* час знову невідомий: вже DUE-задача все одно повторює нагадування */
    int dues = 0;
    for (int i = 0; i < 125; i++)
        if (ts_tick(&ts, 1, -1, 0, &out)) dues++;
    CHECK(dues == 2);
}

static void test_missed_reminder_fires_on_load(void)
{
    /* розклад прийшов, коли час задач уже минув */
    task_scheduler_t ts; event_t out;
    fd_unix_t when[2] = { T0 - 3600, T0 - 10 };
    schedule_t s = make_sched(1, 2, when);
    load(&ts, NULL, &s);
    CHECK(ts_tick(&ts, 1, T0, 0, &out)); CHECK(out.id == 10);
    CHECK(ts_tick(&ts, 1, T0 + 1, 0, &out)); CHECK(out.id == 11);
}

static void test_big_dt_single_event(void)
{
    task_scheduler_t ts; event_t out;
    schedule_t s = one_task(1, T0);
    load(&ts, NULL, &s);
    CHECK(ts_tick(&ts, 1, T0, 0, &out));
    CHECK(ts_tick(&ts, 1000, T0 + 1000, 0, &out)); /* пропущено багато тіків */
    CHECK(out.value == 1);
    CHECK(!ts_tick(&ts, 1, T0 + 1001, 0, &out));   /* а не серія повторів */
    CHECK(!ts_tick(&ts, 0, T0 + 1001, 0, &out));   /* dt = 0 нічого не міняє */
}

static void test_merge_keeps_state(void)
{
    task_scheduler_t ts; event_t out;
    fd_unix_t when[3] = { T0, T0, T0 + 5000 };
    schedule_t s = make_sched(1, 3, when);
    load(&ts, NULL, &s);
    run(&ts, T0, 3);                                /* 10, 11 — DUE */
    send(&ts, EVT_TASK_SEEN, 11, 0, &out);          /* 11 — SEEN */
    send(&ts, EVT_TASK_DONE, 11, 0, &out);          /* 11 — DONE */
    ts_tick(&ts, 30, T0 + 3, 0, &out);              /* 10: since = 33 с */

    /* версія 2: той самий час для 10 і 11, 12 змінено, 13 нова */
    fd_unix_t w2[4] = { T0, T0 + 7, T0 + 9000, T0 + 20 };
    schedule_t s2 = make_sched(2, 4, w2);
    s2.tasks[1].id = 11; s2.tasks[1].time_utc = T0 + 7;   /* DONE, час змінено */
    s2.tasks[2].id = 12;
    s2.tasks[3].id = 13;
    s2.tasks[0].status = TASK_DONE;                 /* вхідний status ігнорується */
    CHECK(ts_set_schedule(&ts, &s2, 0, NULL) == FD_OK);

    CHECK(ts_find(&ts, 10)->status == TASK_DUE);        /* зберігся */
    CHECK(ts_find(&ts, 11)->status == TASK_DONE);       /* закрита лишається */
    CHECK(ts_find(&ts, 12)->status == TASK_SCHEDULED);
    CHECK(ts_find(&ts, 12)->time_utc == T0 + 9000);
    CHECK(ts_find(&ts, 13)->status == TASK_SCHEDULED);

    /* таймер повтору перенесено: до оновлення набігло 32 с (2 + 30),
     * тож повтор на 28-му тіку після нього (32 + 28 = 60) */
    int first = -1;
    for (int i = 0; i < 60; i++)
        if (ts_tick(&ts, 1, T0 + 100 + i, 0, &out) && out.id == 10 && first < 0)
            first = i;                               /* 13 спрацює раніше, нас цікавить 10 */
    CHECK(first == 27);                              /* i = 27 — 28-й тік */

    /* версія 3: час задачі 10 змінили → знову SCHEDULED; 13 зникла */
    fd_unix_t w3[2] = { T0 + 4000, T0 + 9000 };
    schedule_t s3 = make_sched(3, 2, w3);
    s3.tasks[1].id = 12;
    CHECK(ts_set_schedule(&ts, &s3, 0, NULL) == FD_OK);
    CHECK(ts_find(&ts, 10)->status == TASK_SCHEDULED);
    CHECK(ts_find(&ts, 11) == NULL);
    CHECK(ts_find(&ts, 13) == NULL);
    CHECK(ts_schedule(&ts)->task_count == 2);
}

static void test_restore_due_reminds_again(void)
{
    /* E6: перезапуск під час нагадування */
    task_scheduler_t ts; event_t out;
    fd_unix_t when[4] = { T0, T0, T0, T0 };
    schedule_t s = make_sched(5, 4, when);
    s.tasks[0].status = TASK_DUE;
    s.tasks[1].status = TASK_SEEN;
    s.tasks[2].status = TASK_DONE;
    s.tasks[3].status = TASK_SCHEDULED;

    ts_init(&ts, NULL);
    CHECK(ts_restore(&ts, &s) == FD_OK);
    CHECK(ts_schedule_version(&ts) == 5);
    CHECK(ts_find(&ts, 10)->status == TASK_DUE);

    /* порядок на тіку: спершу задача, час якої настав (13), потім
     * відновлена DUE (10); SEEN (11) і DONE (12) мовчать */
    CHECK(ts_tick(&ts, 1, T0 + 500, 0, &out));
    CHECK(out.type == EVT_TASK_DUE);
    CHECK(out.id == 13);
    CHECK(out.value == 0);
    CHECK(ts_tick(&ts, 1, T0 + 501, 0, &out));
    CHECK(out.type == EVT_TASK_DUE);
    CHECK(out.id == 10);
    CHECK(out.value == 0);
    CHECK(ts_find(&ts, 11)->status == TASK_SEEN);
    CHECK(ts_find(&ts, 12)->status == TASK_DONE);

    /* далі — звичайні повтори раз на 60 с: 13 на T0+560, 10 на T0+561 */
    CHECK(run(&ts, T0 + 502, 58) == 0);
    CHECK(run(&ts, T0 + 560, 2) == 2);

    /* після відновлення можна приймати лише нову версію */
    schedule_t same = make_sched(5, 1, when);
    CHECK(ts_set_schedule(&ts, &same, 0, NULL) == FD_ERR_STATE);
    schedule_t next = make_sched(6, 1, when);
    CHECK(ts_set_schedule(&ts, &next, 0, NULL) == FD_OK);
}

static void test_restore_validation(void)
{
    task_scheduler_t ts;
    schedule_t s = one_task(1, T0);
    ts_init(&ts, NULL);
    CHECK(ts_restore(NULL, &s) == FD_ERR_INVALID_ARG);
    CHECK(ts_restore(&ts, NULL) == FD_ERR_INVALID_ARG);
    s.tasks[0].status = (task_status_t)9;
    CHECK(ts_restore(&ts, &s) == FD_ERR_INVALID_ARG);
    s = one_task(1, T0);
    s.tasks[0].id = 0;
    CHECK(ts_restore(&ts, &s) == FD_ERR_INVALID_ARG);
    CHECK(ts_schedule_version(&ts) == 0);
}

static void test_local_time(void)
{
    task_t t;
    memset(&t, 0, sizeof(t));
    t.time_utc = T0 - (T0 % 86400) + 12 * 3600 + 30 * 60;   /* 12:30 UTC */
    t.tz_offset_min = 180;
    CHECK(ts_local_minute_of_day(&t) == 15 * 60 + 30);

    t.time_utc = T0 - (T0 % 86400) + 10 * 60;               /* 00:10 UTC */
    t.tz_offset_min = -180;
    CHECK(ts_local_minute_of_day(&t) == 21 * 60 + 10);      /* попередня доба */

    t.time_utc = T0 - (T0 % 86400) + 23 * 3600;             /* 23:00 UTC */
    t.tz_offset_min = 120;
    CHECK(ts_local_minute_of_day(&t) == 60);                /* 01:00 наступного дня */
    CHECK(ts_local_minute_of_day(NULL) == 0);
}

static void test_text_termination(void)
{
    task_scheduler_t ts; event_t out;
    schedule_t s = one_task(1, T0);
    memset(s.tasks[0].text, 'x', FD_TEXT_MAX);       /* без '\0' */
    memset(s.goals[0].title, 'y', FD_TEXT_MAX);
    load(&ts, NULL, &s);
    CHECK(strlen(ts_find(&ts, 10)->text) == FD_TEXT_MAX - 1);
    CHECK(strlen(ts_schedule(&ts)->goals[0].title) == FD_TEXT_MAX - 1);
    CHECK(ts_tick(&ts, 1, T0, 0, &out));
    CHECK(strlen(out.text) == FD_TEXT_MAX - 1);
}

static void test_ignored_events_and_null_safety(void)
{
    task_scheduler_t ts; event_t out;
    schedule_t s = one_task(1, T0);
    load(&ts, NULL, &s);

    CHECK(!send(&ts, EVT_PRESENCE_LOST, 10, 0, &out));
    CHECK(!send(&ts, EVT_FOCUS_START, 0, 0, &out));
    CHECK(!send(&ts, EVT_TASK_DUE, 10, 0, &out));    /* DUE ставить лише тік */

    ts_init(NULL, NULL);
    CHECK(!ts_tick(NULL, 1, T0, 0, &out));
    CHECK(!ts_tick(&ts, 1, T0, 0, NULL));
    CHECK(!ts_handle_event(NULL, &out, &out));
    CHECK(!ts_handle_event(&ts, NULL, &out));
    CHECK(!ts_handle_event(&ts, &out, NULL));
    CHECK(ts_schedule(NULL) == NULL);
    CHECK(ts_schedule_version(NULL) == 0);
    CHECK(ts_find(NULL, 10) == NULL);
    CHECK(ts_find(&ts, 0) == NULL);
    CHECK(ts_find(&ts, 99) == NULL);
    CHECK(ts_active(NULL) == NULL);
    CHECK(ts_due_count(NULL) == 0);
    CHECK(strcmp(ts_status_name(TASK_SEEN), "SEEN") == 0);
    CHECK(strcmp(ts_status_name(TASK_DONE), "DONE") == 0);
}

static void test_full_scenario(void)
{
    /* Сценарій: задача настала → нагадування ігнорують → повтор →
     * «бачу» → час минає → «виконано» → тиша. */
    task_scheduler_t ts; event_t out;
    schedule_t s = one_task(1, T0 + 30);
    load(&ts, NULL, &s);

    int dues = run(&ts, T0, 100);                    /* 30-та с та 90-та с */
    CHECK(dues == 2);
    CHECK(ts_find(&ts, 10)->status == TASK_DUE);

    CHECK(send(&ts, EVT_TASK_SEEN, 0, 0, &out));
    CHECK(run(&ts, T0 + 100, 1000) == 0);
    CHECK(ts_find(&ts, 10)->status == TASK_SEEN);

    CHECK(send(&ts, EVT_TASK_DONE, 0, 0, &out));
    CHECK(ts_find(&ts, 10)->status == TASK_DONE);
    CHECK(run(&ts, T0 + 1100, 1000) == 0);
}

int main(void)
{
    test_empty_and_defaults();
    test_set_schedule_and_versions();
    test_invalid_schedules();
    test_due_exactly_at_time();
    test_latency_within_one_tick();
    test_repeat_until_seen();
    test_custom_repeat();
    test_seen_rules();
    test_done_rules();
    test_simultaneous_tasks();
    test_unknown_time();
    test_missed_reminder_fires_on_load();
    test_big_dt_single_event();
    test_merge_keeps_state();
    test_restore_due_reminds_again();
    test_restore_validation();
    test_local_time();
    test_text_termination();
    test_ignored_events_and_null_safety();
    test_full_scenario();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
