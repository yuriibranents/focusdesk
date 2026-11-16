/* Host-тести focus_session (без ESP-IDF). */
#include "focus_session.h"
#include <stdio.h>
#include <string.h>

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define MIN(n) ((uint32_t)(n) * 60u)

typedef struct { int start, pause, resume, susp, done; } counts_t;

static void count(counts_t *c, const event_t *e)
{
    if (e->type == EVT_FOCUS_START)     c->start++;
    if (e->type == EVT_FOCUS_PAUSE)     c->pause++;
    if (e->type == EVT_FOCUS_RESUME)    c->resume++;
    if (e->type == EVT_FOCUS_SUSPENDED) c->susp++;
    if (e->type == EVT_FOCUS_DONE)      c->done++;
}

static counts_t run(focus_session_t *fs, uint32_t secs)
{
    counts_t c = {0, 0, 0, 0, 0};
    event_t out;
    for (uint32_t i = 0; i < secs; i++)
        if (fs_tick(fs, 1, (fd_time_us_t)i * 1000000, &out)) count(&c, &out);
    return c;
}

static bool send(focus_session_t *fs, event_type_t t, fd_time_us_t ts,
                 event_t *out)
{
    event_t in;
    memset(&in, 0, sizeof(in));
    in.type = t;
    in.ts_us = ts;
    return fs_handle_event(fs, &in, out);
}

static goal_t make_goal(uint16_t id, const char *title, uint16_t minutes)
{
    goal_t g;
    memset(&g, 0, sizeof(g));
    g.id = id;
    g.minutes = minutes;
    snprintf(g.title, sizeof(g.title), "%s", title);
    return g;
}

/* Сесія вже в стані RUNNING. */
static void start_running(focus_session_t *fs, const config_t *cfg,
                          uint16_t minutes)
{
    event_t out;
    goal_t g = make_goal(1, "Test", minutes);
    fs_init(fs, cfg);
    fs_select_goal(fs, &g);
    send(fs, EVT_FOCUS_START, 0, &out);
}

static void test_select_and_start(void)
{
    focus_session_t fs; event_t out;
    fs_init(&fs, NULL);
    CHECK(fs_state(&fs) == FS_IDLE);
    CHECK(!send(&fs, EVT_FOCUS_START, 0, &out));        /* без цілі */
    CHECK(fs_state(&fs) == FS_IDLE);

    goal_t bad = make_goal(2, "Нуль", 0);
    CHECK(!fs_select_goal(&fs, &bad));                  /* 0 хвилин */
    CHECK(!fs_select_goal(&fs, NULL));

    goal_t g = make_goal(1, "Англійська", 120);
    CHECK(fs_select_goal(&fs, &g));
    CHECK(fs_state(&fs) == FS_READY);
    CHECK(fs_remaining_seconds(&fs) == MIN(120));
    CHECK(fs_goal(&fs)->id == 1);

    CHECK(fs_select_goal(&fs, &g));                     /* переобрати в READY */

    CHECK(send(&fs, EVT_FOCUS_START, 42, &out));
    CHECK(out.type == EVT_FOCUS_START && out.id == 1 && out.value == 120);
    CHECK(out.ts_us == 42);
    CHECK(strcmp(out.text, "Англійська") == 0);
    CHECK(fs_state(&fs) == FS_RUNNING);

    CHECK(!send(&fs, EVT_FOCUS_START, 0, &out));        /* повторний старт */
    CHECK(!fs_select_goal(&fs, &g));                    /* під час сесії */
}

static void test_countdown_and_done(void)
{
    focus_session_t fs; event_t out;
    start_running(&fs, NULL, 2);
    counts_t c = run(&fs, 119);
    CHECK(c.done == 0 && fs_remaining_seconds(&fs) == 1);
    CHECK(fs_state(&fs) == FS_RUNNING);

    CHECK(fs_tick(&fs, 1, 555, &out));
    CHECK(out.type == EVT_FOCUS_DONE && out.id == 1 && out.value == 2);
    CHECK(out.ts_us == 555 && strcmp(out.text, "Test") == 0);
    CHECK(fs_state(&fs) == FS_DONE && fs_remaining_seconds(&fs) == 0);

    CHECK(!fs_tick(&fs, 1, 0, &out));                   /* DONE -> IDLE */
    CHECK(fs_state(&fs) == FS_IDLE);
    c = run(&fs, 100);
    CHECK(c.done == 0);
}

static void test_pause_and_resume(void)
{
    focus_session_t fs; event_t out;
    start_running(&fs, NULL, 10);
    run(&fs, 30);
    CHECK(fs_remaining_seconds(&fs) == MIN(10) - 30);

    CHECK(send(&fs, EVT_PRESENCE_LOST, 7, &out));
    CHECK(out.type == EVT_FOCUS_PAUSE && out.value == (int32_t)(MIN(10) - 30));
    CHECK(out.ts_us == 7 && fs_state(&fs) == FS_PAUSED);

    counts_t c = run(&fs, 120);
    CHECK(c.susp == 0);
    CHECK(fs_remaining_seconds(&fs) == MIN(10) - 30);   /* час стоїть */
    CHECK(fs_paused_seconds(&fs) == 120);

    CHECK(send(&fs, EVT_PRESENCE_BACK, 0, &out));
    CHECK(out.type == EVT_FOCUS_RESUME && fs_state(&fs) == FS_RUNNING);
    run(&fs, 30);
    CHECK(fs_remaining_seconds(&fs) == MIN(10) - 60);
    CHECK(fs_paused_seconds(&fs) == 120);
}

static void test_return_just_before_suspend(void)
{
    focus_session_t fs; event_t out;
    start_running(&fs, NULL, 30);
    send(&fs, EVT_PRESENCE_LOST, 0, &out);
    counts_t c = run(&fs, MIN(10) - 1);
    CHECK(c.susp == 0 && fs_state(&fs) == FS_PAUSED);
    CHECK(send(&fs, EVT_PRESENCE_BACK, 0, &out));
    CHECK(fs_state(&fs) == FS_RUNNING);
}

static void test_suspend_and_resume_by_button(void)
{
    focus_session_t fs; event_t out;
    start_running(&fs, NULL, 30);
    run(&fs, 60);
    send(&fs, EVT_PRESENCE_LOST, 0, &out);
    counts_t c = run(&fs, MIN(10) - 1);
    CHECK(c.susp == 0);
    c = run(&fs, 1);
    CHECK(c.susp == 1 && fs_state(&fs) == FS_SUSPENDED);   /* рівно 10 хв */
    CHECK(fs_remaining_seconds(&fs) == MIN(30) - 60);

    CHECK(!send(&fs, EVT_PRESENCE_BACK, 0, &out));         /* кнопка потрібна */
    CHECK(fs_state(&fs) == FS_SUSPENDED);
    c = run(&fs, 100);
    CHECK(c.susp == 0 && c.done == 0);                     /* подія одна */
    CHECK(fs_paused_seconds(&fs) == MIN(10) + 100);
    CHECK(fs_remaining_seconds(&fs) == MIN(30) - 60);

    CHECK(send(&fs, EVT_FOCUS_START, 9, &out));
    CHECK(out.type == EVT_FOCUS_RESUME && out.ts_us == 9);
    CHECK(fs_state(&fs) == FS_RUNNING);
    run(&fs, 10);
    CHECK(fs_remaining_seconds(&fs) == MIN(30) - 70);
    CHECK(fs_paused_seconds(&fs) == MIN(10) + 100);
}

static void test_cancel(void)
{
    focus_session_t fs; event_t out;
    fs_init(&fs, NULL);
    CHECK(!fs_cancel(&fs));                                /* вже IDLE */

    goal_t g = make_goal(3, "Читання", 25);
    fs_select_goal(&fs, &g);
    CHECK(fs_cancel(&fs) && fs_state(&fs) == FS_IDLE);     /* з READY */

    start_running(&fs, NULL, 10);
    CHECK(fs_cancel(&fs) && fs_state(&fs) == FS_IDLE);     /* з RUNNING */
    CHECK(fs_remaining_seconds(&fs) == 0);
    CHECK(!send(&fs, EVT_FOCUS_START, 0, &out));

    start_running(&fs, NULL, 10);
    send(&fs, EVT_PRESENCE_LOST, 0, &out);
    run(&fs, MIN(10));
    CHECK(fs_state(&fs) == FS_SUSPENDED);
    CHECK(fs_cancel(&fs) && fs_state(&fs) == FS_IDLE);     /* з SUSPENDED */
}

static void test_ignored_events(void)
{
    focus_session_t fs; event_t out;
    fs_init(&fs, NULL);
    CHECK(!send(&fs, EVT_PRESENCE_LOST, 0, &out));         /* IDLE */
    CHECK(!send(&fs, EVT_PRESENCE_BACK, 0, &out));
    goal_t g = make_goal(1, "A", 5);
    fs_select_goal(&fs, &g);
    CHECK(!send(&fs, EVT_PRESENCE_LOST, 0, &out));         /* READY */
    CHECK(fs_state(&fs) == FS_READY);
    counts_t c = run(&fs, 1000);
    CHECK(c.done == 0 && fs_remaining_seconds(&fs) == MIN(5));

    send(&fs, EVT_FOCUS_START, 0, &out);
    CHECK(!send(&fs, EVT_PRESENCE_BACK, 0, &out));         /* RUNNING */
    CHECK(!send(&fs, EVT_TASK_DUE, 0, &out));              /* чужа подія */
    CHECK(!send(&fs, EVT_BREAK_DUE, 0, &out));
    CHECK(fs_state(&fs) == FS_RUNNING);

    send(&fs, EVT_PRESENCE_LOST, 0, &out);
    CHECK(!send(&fs, EVT_PRESENCE_LOST, 0, &out));         /* повторний LOST */
    CHECK(!send(&fs, EVT_FOCUS_START, 0, &out));           /* кнопка в PAUSED */
    CHECK(fs_state(&fs) == FS_PAUSED);
}

static void test_big_dt(void)
{
    focus_session_t fs; event_t out;
    start_running(&fs, NULL, 1);
    CHECK(fs_tick(&fs, 4000, 11, &out));
    CHECK(out.type == EVT_FOCUS_DONE && out.ts_us == 11);
    CHECK(fs_remaining_seconds(&fs) == 0 && fs_state(&fs) == FS_DONE);

    start_running(&fs, NULL, 60);
    send(&fs, EVT_PRESENCE_LOST, 0, &out);
    CHECK(fs_tick(&fs, 5000, 12, &out));
    CHECK(out.type == EVT_FOCUS_SUSPENDED && fs_state(&fs) == FS_SUSPENDED);
}

static void test_custom_suspend(void)
{
    config_t cfg = fd_config_default();
    cfg.suspend_after_min = 2;
    focus_session_t fs; event_t out;
    start_running(&fs, &cfg, 30);
    send(&fs, EVT_PRESENCE_LOST, 0, &out);
    counts_t c = run(&fs, MIN(2) - 1);
    CHECK(c.susp == 0);
    c = run(&fs, 1);
    CHECK(c.susp == 1);
}

static void test_reselect_after_done(void)
{
    focus_session_t fs; event_t out;
    start_running(&fs, NULL, 1);
    send(&fs, EVT_PRESENCE_LOST, 0, &out);
    run(&fs, 30);
    send(&fs, EVT_PRESENCE_BACK, 0, &out);
    run(&fs, 60);
    CHECK(fs_state(&fs) == FS_DONE);
    CHECK(fs_paused_seconds(&fs) == 30);

    goal_t g = make_goal(5, "Нова ціль", 15);
    CHECK(fs_select_goal(&fs, &g));                        /* з DONE */
    CHECK(fs_state(&fs) == FS_READY);
    CHECK(fs_paused_seconds(&fs) == 0);                    /* лічильники скинуто */
    CHECK(fs_remaining_seconds(&fs) == MIN(15));
}

static void test_long_title_and_null_safety(void)
{
    focus_session_t fs; event_t out;
    char long_title[64];
    memset(long_title, 'x', sizeof(long_title) - 1);
    long_title[sizeof(long_title) - 1] = '\0';
    goal_t g;
    memset(&g, 0, sizeof(g));
    g.id = 9; g.minutes = 5;
    memset(g.title, 'y', sizeof(g.title));                /* без '\0' */
    fs_init(&fs, NULL);
    CHECK(fs_select_goal(&fs, &g));
    CHECK(strlen(fs_goal(&fs)->title) == sizeof(g.title) - 1);
    send(&fs, EVT_FOCUS_START, 0, &out);
    CHECK(strlen(out.text) == FD_TEXT_MAX - 1);

    fs_init(NULL, NULL);
    CHECK(fs_state(NULL) == FS_IDLE);
    CHECK(!fs_cancel(NULL));
    CHECK(!fs_tick(NULL, 1, 0, &out));
    CHECK(!fs_handle_event(&fs, NULL, &out));
    CHECK(fs_goal(NULL) == NULL);
    CHECK(strcmp(fs_state_name(FS_SUSPENDED), "SUSPENDED") == 0);
}

int main(void)
{
    test_select_and_start();
    test_countdown_and_done();
    test_pause_and_resume();
    test_return_just_before_suspend();
    test_suspend_and_resume_by_button();
    test_cancel();
    test_ignored_events();
    test_big_dt();
    test_custom_suspend();
    test_reselect_after_done();
    test_long_title_and_null_safety();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
