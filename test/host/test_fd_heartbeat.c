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

int main(void)
{
    test_fresh_and_timeout();
    test_beat_keeps_alive();
    test_single_task_hang();
    test_wraparound();
    test_defaults_and_bad_args();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
