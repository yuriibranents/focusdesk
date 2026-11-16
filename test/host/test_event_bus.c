/* Host-тести event_bus з фейковим FreeRTOS (test/host/fake). */
#include "event_bus.h"
#include <stdio.h>
#include <string.h>

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static event_t ev(event_type_t t, int32_t v, fd_time_us_t ts)
{
    event_t e; memset(&e, 0, sizeof(e));
    e.type = t; e.value = v; e.ts_us = ts;
    return e;
}

static void test_init_state(void)
{
    event_t e = ev(EVT_TICK, 0, 1), o;
    CHECK(event_bus_post(&e, 0) == FD_ERR_STATE);        /* до init */
    CHECK(event_bus_receive(&o, 0) == FD_ERR_STATE);
    CHECK(event_bus_init() == FD_OK);
    CHECK(event_bus_init() == FD_ERR_STATE);             /* повторно */
    event_bus_deinit();
    CHECK(event_bus_post(&e, 0) == FD_ERR_STATE);        /* після deinit */
    CHECK(event_bus_init() == FD_OK);                    /* можна знову */
    event_bus_deinit();
}

static void test_fifo_and_timestamp(void)
{
    event_bus_init();
    event_t a = ev(EVT_PRESENCE_LOST, 1, 0);             /* ts == 0 */
    event_t b = ev(EVT_PRESENCE_BACK, 2, 777);           /* ts задано */
    snprintf(b.text, sizeof(b.text), "hello");
    CHECK(event_bus_post(&a, 0) == FD_OK);
    CHECK(event_bus_post(&b, 0) == FD_OK);
    CHECK(a.ts_us == 0);                                 /* оригінал не змінено */

    event_t o;
    CHECK(event_bus_receive(&o, 0) == FD_OK);
    CHECK(o.type == EVT_PRESENCE_LOST && o.value == 1);
    CHECK(o.ts_us == 123456);                            /* час від шини */
    CHECK(event_bus_receive(&o, 0) == FD_OK);
    CHECK(o.type == EVT_PRESENCE_BACK && o.ts_us == 777);
    CHECK(strcmp(o.text, "hello") == 0);
    CHECK(event_bus_receive(&o, 100) == FD_ERR_TIMEOUT); /* порожньо */
    CHECK(event_bus_receive(&o, EB_WAIT_FOREVER) == FD_ERR_TIMEOUT);
    event_bus_deinit();
}

static void test_events_full(void)
{
    event_bus_init();
    for (int i = 0; i < FD_Q_EVENTS_LEN; i++) {
        event_t e = ev(EVT_TICK, i, 1);
        CHECK(event_bus_post(&e, 0) == FD_OK);
    }
    event_t extra = ev(EVT_TASK_DUE, 99, 1);
    CHECK(event_bus_post(&extra, 0) == FD_ERR_QUEUE_FULL);
    CHECK(event_bus_post(&extra, 10) == FD_ERR_QUEUE_FULL);
    CHECK(event_bus_dropped_events() == 2);

    event_t o;
    CHECK(event_bus_receive(&o, 0) == FD_OK && o.value == 0);   /* стара лишилась */
    CHECK(event_bus_post(&extra, 0) == FD_OK);                  /* є місце */
    for (int i = 1; i < FD_Q_EVENTS_LEN; i++)
        CHECK(event_bus_receive(&o, 0) == FD_OK && o.value == i);
    CHECK(event_bus_receive(&o, 0) == FD_OK && o.type == EVT_TASK_DUE);
    event_bus_deinit();

    event_bus_init();
    CHECK(event_bus_dropped_events() == 0);              /* лічильник скинуто */
    event_bus_deinit();
}

static void test_ui_queue(void)
{
    event_bus_init();
    ui_cmd_t c; memset(&c, 0, sizeof(c));
    for (int i = 0; i < FD_Q_UI_LEN; i++) {
        c.screen = SCREEN_FOCUS; c.value = i;
        CHECK(event_bus_post_ui(&c, 0) == FD_OK);
    }
    c.screen = SCREEN_ERROR;
    CHECK(event_bus_post_ui(&c, 0) == FD_ERR_QUEUE_FULL);
    CHECK(event_bus_dropped_ui() == 1);
    CHECK(event_bus_dropped_events() == 0);              /* черги незалежні */

    ui_cmd_t o;
    for (int i = 0; i < FD_Q_UI_LEN; i++)
        CHECK(event_bus_receive_ui(&o, 0) == FD_OK && o.value == i
              && o.screen == SCREEN_FOCUS);
    CHECK(event_bus_receive_ui(&o, 0) == FD_ERR_TIMEOUT);
    event_bus_deinit();
}

static void test_bad_args(void)
{
    event_bus_init();
    CHECK(event_bus_post(NULL, 0) == FD_ERR_INVALID_ARG);
    CHECK(event_bus_receive(NULL, 0) == FD_ERR_INVALID_ARG);
    CHECK(event_bus_post_ui(NULL, 0) == FD_ERR_INVALID_ARG);
    CHECK(event_bus_receive_ui(NULL, 0) == FD_ERR_INVALID_ARG);
    event_bus_deinit();
}

int main(void)
{
    test_init_state();
    test_fifo_and_timestamp();
    test_events_full();
    test_ui_queue();
    test_bad_args();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
