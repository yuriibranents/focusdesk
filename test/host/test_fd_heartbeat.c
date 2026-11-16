#include "fd_heartbeat.h"
#include <stdio.h>

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void test_fresh_and_timeout(void)
{
    fd_hb_t hb; fd_hb_init(&hb, 10000, 1000);
    CHECK(fd_hb_stale(&hb, 1000) == 0);
    CHECK(fd_hb_stale(&hb, 10999) == 0);                 /* 9999 мс */
    CHECK(fd_hb_stale(&hb, 11000) == FD_HB_ALL);         /* рівно 10 с */
}

static void test_beat_keeps_alive(void)
{
    fd_hb_t hb; fd_hb_init(&hb, 10000, 0);
    fd_hb_beat(&hb, FD_HB_LOGIC | FD_HB_UI, 9000);
    uint32_t s = fd_hb_stale(&hb, 12000);
    CHECK(s == (FD_HB_PRESENCE | FD_HB_SYNC));           /* мовчать лише ці */
    fd_hb_beat(&hb, FD_HB_ALL, 12000);
    CHECK(fd_hb_stale(&hb, 21999) == 0);
    CHECK(fd_hb_stale(&hb, 22000) == FD_HB_ALL);
}

static void test_single_task_hang(void)
{
    fd_hb_t hb; fd_hb_init(&hb, 10000, 0);
    for (uint32_t t = 1000; t <= 30000; t += 1000)
        fd_hb_beat(&hb, FD_HB_ALL & ~FD_HB_SYNC, t);     /* sync завис */
    CHECK(fd_hb_stale(&hb, 30000) == FD_HB_SYNC);
}

static void test_wraparound(void)
{
    fd_hb_t hb; fd_hb_init(&hb, 10000, 0xFFFFFFFFu - 4999u);  /* за 5 с до wrap */
    CHECK(fd_hb_stale(&hb, 4000) == 0);                  /* пройшло 9 с */
    CHECK(fd_hb_stale(&hb, 5000) == FD_HB_ALL);          /* 10 с через 0 */
    fd_hb_beat(&hb, FD_HB_ALL, 5000);
    CHECK(fd_hb_stale(&hb, 14999) == 0);
}

static void test_defaults_and_bad_args(void)
{
    fd_hb_t hb; fd_hb_init(&hb, 0, 0);                   /* 0 -> за замовчуванням */
    CHECK(hb.timeout_ms == FD_HB_TIMEOUT_MS);
    fd_hb_beat(&hb, 0xF0u, 5);                           /* невідомі біти */
    CHECK(fd_hb_stale(&hb, 5) == 0);
    fd_hb_init(NULL, 1, 1); fd_hb_beat(NULL, 1, 1);
    CHECK(fd_hb_stale(NULL, 1) == 0);
}

static void test_wd_episode(void)
{
    fd_wd_t wd; fd_wd_init(&wd, 5000);
    CHECK(fd_wd_step(&wd, 0, 1000) == FD_WD_OK);
    CHECK(fd_wd_step(&wd, FD_HB_SYNC, 2000) == FD_WD_STALE_NEW);   /* вперше */
    CHECK(fd_wd_step(&wd, FD_HB_SYNC, 3000) == FD_WD_STALE_WAIT);
    CHECK(fd_wd_step(&wd, FD_HB_SYNC | FD_HB_UI, 6999) == FD_WD_STALE_WAIT);
    CHECK(fd_wd_step(&wd, FD_HB_SYNC, 7000) == FD_WD_RESTART);     /* 5 с після 2000 */
    CHECK(fd_wd_step(&wd, FD_HB_SYNC, 8000) == FD_WD_RESTART);
}

static void test_wd_recovery_resets_episode(void)
{
    fd_wd_t wd; fd_wd_init(&wd, 5000);
    CHECK(fd_wd_step(&wd, FD_HB_LOGIC, 1000) == FD_WD_STALE_NEW);
    CHECK(fd_wd_step(&wd, FD_HB_LOGIC, 4000) == FD_WD_STALE_WAIT);
    CHECK(fd_wd_step(&wd, 0, 5000) == FD_WD_OK);                   /* ожила */
    CHECK(fd_wd_step(&wd, FD_HB_LOGIC, 9000) == FD_WD_STALE_NEW);  /* новий епізод */
    CHECK(fd_wd_step(&wd, FD_HB_LOGIC, 13999) == FD_WD_STALE_WAIT);
    CHECK(fd_wd_step(&wd, FD_HB_LOGIC, 14000) == FD_WD_RESTART);
}

static void test_wd_defaults_wrap_null(void)
{
    fd_wd_t wd; fd_wd_init(&wd, 0);
    CHECK(wd.grace_ms == FD_WD_GRACE_MS);
    CHECK(fd_wd_step(&wd, FD_HB_UI, 0xFFFFFFFFu - 1000u) == FD_WD_STALE_NEW);
    CHECK(fd_wd_step(&wd, FD_HB_UI, 3998u) == FD_WD_STALE_WAIT);   /* 5 с - 1 мс через 0 */
    CHECK(fd_wd_step(&wd, FD_HB_UI, 3999u) == FD_WD_RESTART);
    fd_wd_init(NULL, 1);
    CHECK(fd_wd_step(NULL, FD_HB_UI, 1) == FD_WD_OK);
}

/* Сценарій цілком: задача зависає, годування припиняється. */
static void test_hang_scenario(void)
{
    fd_hb_t hb; fd_wd_t wd;
    fd_hb_init(&hb, 10000, 0);
    fd_wd_init(&wd, 5000);
    fd_wd_action_t last = FD_WD_OK;
    uint32_t restart_at = 0, new_at = 0;
    for (uint32_t t = 1000; t <= 40000; t += 1000) {
        fd_hb_beat(&hb, FD_HB_ALL & ~FD_HB_UI, t);   /* UI завис */
        fd_wd_action_t a = fd_wd_step(&wd, fd_hb_stale(&hb, t), t);
        if (a == FD_WD_STALE_NEW && !new_at) new_at = t;
        if (a == FD_WD_RESTART && !restart_at) restart_at = t;
        last = a;
    }
    CHECK(new_at == 10000);        /* мовчить 10 с */
    CHECK(restart_at == 15000);    /* ще 5 с пільгового часу */
    CHECK(last == FD_WD_RESTART);
}

int main(void)
{
    test_fresh_and_timeout();
    test_beat_keeps_alive();
    test_single_task_hang();
    test_wraparound();
    test_defaults_and_bad_args();
    test_wd_episode();
    test_wd_recovery_resets_episode();
    test_wd_defaults_wrap_null();
    test_hang_scenario();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
