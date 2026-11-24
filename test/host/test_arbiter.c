/* Host-тести arbiter (без ESP-IDF). */
#include "arbiter.h"
#include <stdio.h>
#include <string.h>

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static bool ev(arbiter_t *a, event_type_t t, uint16_t id, int32_t value,
               const char *text, ui_cmd_t *out)
{
    event_t e;
    memset(&e, 0, sizeof(e));
    e.type = t;
    e.id = id;
    e.value = value;
    if (text) snprintf(e.text, sizeof(e.text), "%s", text);
    return arb_handle_event(a, &e, out);
}

static bool is_rgb(const ui_cmd_t *c, uint8_t r, uint8_t g, uint8_t b)
{
    return c->r == r && c->g == g && c->b == b;
}

/* Лічильник видачі команд за n тіків по 1 с. */
static int ticks(arbiter_t *a, int n)
{
    int c = 0;
    ui_cmd_t o;
    for (int i = 0; i < n; i++) if (arb_tick(a, 1, &o)) c++;
    return c;
}

/* ------------------------------------------------------------------ */

static void test_idle_and_ignored(void)
{
    arbiter_t a; ui_cmd_t o;
    arb_init(&a);
    CHECK(arb_current(&a, &o));
    CHECK(o.screen == SCREEN_IDLE);
    CHECK(o.ring_mode == RING_STATIC);
    CHECK(is_rgb(&o, ARB_RGB_IDLE));
    CHECK(o.sound == SOUND_NONE);
    CHECK(arb_layer(&a) == ARB_LAYER_IDLE);

    CHECK(ticks(&a, 100) == 0);                       /* у спокої нічого не шлемо */
    CHECK(!ev(&a, EVT_PRESENCE_LOST, 0, 0, NULL, &o));
    CHECK(!ev(&a, EVT_TICK, 0, 0, NULL, &o));
    CHECK(!ev(&a, EVT_SCHEDULE_UPDATED, 0, 3, NULL, &o));
    CHECK(!ev(&a, EVT_SYSTEM_ERROR, 0, 1, NULL, &o));
    CHECK(!ev(&a, EVT_PRESENCE_BACK, 0, 0, NULL, &o)); /* "Повернись" не було */
    CHECK(!ev(&a, EVT_BREAK_DONE, 0, 0, NULL, &o));
    CHECK(arb_layer(&a) == ARB_LAYER_IDLE);
}

static void test_focus_lifecycle(void)
{
    arbiter_t a; ui_cmd_t o;
    arb_init(&a);

    CHECK(ev(&a, EVT_FOCUS_START, 3, 2, "Англійська", &o));
    CHECK(o.screen == SCREEN_FOCUS);
    CHECK(o.ring_mode == RING_PROGRESS);
    CHECK(o.ring_value == 0);
    CHECK(o.value == 120);
    CHECK(strcmp(o.text, "Англійська") == 0);
    CHECK(is_rgb(&o, ARB_RGB_FOCUS));
    CHECK(o.sound == SOUND_NONE);

    CHECK(arb_tick(&a, 1, &o));                         /* відлік раз на тік */
    CHECK(o.value == 119);
    CHECK(o.sound == SOUND_NONE);
    CHECK(ticks(&a, 59) == 59);
    CHECK(arb_current(&a, &o));
    CHECK(o.value == 60);
    CHECK(o.ring_value == 50);

    CHECK(ev(&a, EVT_FOCUS_PAUSE, 3, 60, "Англійська", &o));
    CHECK(o.screen == SCREEN_FOCUS_PAUSED);
    CHECK(o.ring_mode == RING_PULSE);
    CHECK(is_rgb(&o, ARB_RGB_PAUSED));
    CHECK(o.value == 60);
    CHECK(ticks(&a, 100) == 0);                         /* на паузі час не йде */
    arb_current(&a, &o);
    CHECK(o.value == 60);

    CHECK(ev(&a, EVT_FOCUS_RESUME, 3, 60, "Англійська", &o));
    CHECK(o.screen == SCREEN_FOCUS);
    CHECK(o.ring_value == 50);
    CHECK(ev(&a, EVT_FOCUS_PAUSE, 3, 60, NULL, &o));
    CHECK(ev(&a, EVT_FOCUS_SUSPENDED, 3, 60, NULL, &o));
    CHECK(o.screen == SCREEN_FOCUS_PAUSED);
    CHECK(o.ring_mode == RING_STATIC);
    CHECK(is_rgb(&o, ARB_RGB_SUSPENDED));
    CHECK(strcmp(o.text, "Англійська") == 0);           /* назва лишилась */
    CHECK(ev(&a, EVT_FOCUS_RESUME, 3, 60, NULL, &o));
    CHECK(o.screen == SCREEN_FOCUS);

    /* завершення: банер із сигналом, потім IDLE */
    CHECK(ev(&a, EVT_FOCUS_DONE, 3, 2, "Англійська", &o));
    CHECK(o.screen == SCREEN_FOCUS);
    CHECK(o.ring_mode == RING_PULSE);
    CHECK(o.ring_value == 100);
    CHECK(o.value == 0);
    CHECK(is_rgb(&o, ARB_RGB_DONE));
    CHECK(o.sound == SOUND_BEEP_DONE);
    CHECK(strcmp(o.text, "Англійська") == 0);
    CHECK(arb_layer(&a) == ARB_LAYER_DONE);

    CHECK(ticks(&a, ARB_DONE_BANNER_S - 1) == 0);       /* банер висить без оновлень */
    CHECK(arb_tick(&a, 1, &o));                          /* і зникає */
    CHECK(o.screen == SCREEN_IDLE);
    CHECK(o.sound == SOUND_NONE);
    CHECK(arb_layer(&a) == ARB_LAYER_IDLE);
}

static void test_break_over_focus(void)
{
    arbiter_t a; ui_cmd_t o;
    arb_init(&a);
    ev(&a, EVT_FOCUS_START, 1, 10, "Фокус", &o);
    ticks(&a, 5);

    CHECK(ev(&a, EVT_BREAK_DUE, 0, 0, NULL, &o));
    CHECK(o.screen == SCREEN_BREAK);
    CHECK(o.ring_mode == RING_PULSE);
    CHECK(is_rgb(&o, ARB_RGB_BREAK));
    CHECK(strcmp(o.text, ARB_TEXT_BREAK) == 0);
    CHECK(o.sound == SOUND_BEEP_SHORT);
    CHECK(arb_layer(&a) == ARB_LAYER_BREAK);

    CHECK(ticks(&a, 10) == 0);                          /* фокус під перервою не оновлює екран */

    CHECK(ev(&a, EVT_BREAK_DONE, 0, 0, NULL, &o));      /* повернення до фокусу */
    CHECK(o.screen == SCREEN_FOCUS);
    CHECK(o.value == 600 - 15);                         /* час ішов у фоні */
    CHECK(o.sound == SOUND_NONE);
    CHECK(arb_layer(&a) == ARB_LAYER_FOCUS);
}

static void test_task_over_everything_and_return(void)
{
    arbiter_t a; ui_cmd_t o;
    arb_init(&a);
    ev(&a, EVT_FOCUS_START, 1, 10, "Фокус", &o);
    ev(&a, EVT_BREAK_DUE, 0, 0, NULL, &o);               /* BREAK, звук відтворено */
    CHECK(ev(&a, EVT_ABSENCE_ALERT, 0, 0, NULL, &o));    /* ABSENCE */
    CHECK(o.screen == SCREEN_ABSENCE_ALERT);
    CHECK(o.ring_mode == RING_BLINK_SLOW);
    CHECK(is_rgb(&o, ARB_RGB_ABSENCE));
    CHECK(strcmp(o.text, ARB_TEXT_ABSENCE) == 0);
    CHECK(o.sound == SOUND_BEEP_SLOW);

    CHECK(ev(&a, EVT_TASK_DUE, 5, 0, "Піти в магазин", &o));
    CHECK(o.screen == SCREEN_TASK);
    CHECK(o.ring_mode == RING_BLINK_FAST);
    CHECK(is_rgb(&o, ARB_RGB_TASK));
    CHECK(o.sound == SOUND_BEEP_FAST);
    CHECK(o.value == 1);
    CHECK(strcmp(o.text, "Піти в магазин") == 0);
    CHECK(arb_layer(&a) == ARB_LAYER_TASK);

    /* повтор нагадування: той самий екран, звук знову */
    CHECK(ev(&a, EVT_TASK_DUE, 5, 1, "Піти в магазин", &o));
    CHECK(o.screen == SCREEN_TASK);
    CHECK(o.sound == SOUND_BEEP_FAST);

    /* "бачу": повернувся "Повернись", без повторного звуку */
    CHECK(ev(&a, EVT_TASK_SEEN, 5, 0, NULL, &o));
    CHECK(o.screen == SCREEN_ABSENCE_ALERT);
    CHECK(o.ring_mode == RING_BLINK_SLOW);
    CHECK(o.sound == SOUND_NONE);

    /* людина повернулась: "Повернись" зникає, перерва знову видна без звуку
     * (вона вже дзвеніла, коли з'явилась уперше) */
    CHECK(ev(&a, EVT_PRESENCE_BACK, 0, 0, NULL, &o));
    CHECK(o.screen == SCREEN_BREAK);
    CHECK(o.sound == SOUND_NONE);

    CHECK(ev(&a, EVT_BREAK_DONE, 0, 0, NULL, &o));
    CHECK(o.screen == SCREEN_FOCUS);
}

static void test_absence_returns_until_back(void)
{
    arbiter_t a; ui_cmd_t o;
    arb_init(&a);
    ev(&a, EVT_ABSENCE_ALERT, 0, 0, NULL, &o);
    ev(&a, EVT_TASK_DUE, 7, 0, "Задача", &o);
    CHECK(o.screen == SCREEN_TASK);
    CHECK(ev(&a, EVT_TASK_SEEN, 7, 0, NULL, &o));
    CHECK(o.screen == SCREEN_ABSENCE_ALERT);            /* повернулось */
    CHECK(ticks(&a, 300) == 0);                          /* і тримається */
    CHECK(ev(&a, EVT_PRESENCE_BACK, 0, 0, NULL, &o));
    CHECK(o.screen == SCREEN_IDLE);
    CHECK(o.sound == SOUND_NONE);
}

static void test_hidden_break_sound_replayed(void)
{
    arbiter_t a; ui_cmd_t o;
    arb_init(&a);
    ev(&a, EVT_TASK_DUE, 1, 0, "Задача", &o);
    CHECK(!ev(&a, EVT_BREAK_DUE, 0, 0, NULL, &o));       /* під задачею: екран той самий */
    CHECK(arb_layer(&a) == ARB_LAYER_TASK);
    CHECK(ev(&a, EVT_TASK_SEEN, 1, 0, NULL, &o));
    CHECK(o.screen == SCREEN_BREAK);
    CHECK(o.sound == SOUND_BEEP_SHORT);                   /* не загубилась */
}

static void test_two_tasks(void)
{
    arbiter_t a; ui_cmd_t o;
    arb_init(&a);
    CHECK(ev(&a, EVT_TASK_DUE, 1, 0, "A", &o));
    CHECK(strcmp(o.text, "A") == 0);
    CHECK(o.value == 1);

    CHECK(ev(&a, EVT_TASK_DUE, 2, 0, "B", &o));          /* друга: лишається найстарша */
    CHECK(strcmp(o.text, "A") == 0);
    CHECK(o.value == 2);
    CHECK(o.sound == SOUND_BEEP_FAST);

    CHECK(ev(&a, EVT_TASK_SEEN, 1, 0, NULL, &o));        /* A побачено: показується B */
    CHECK(o.screen == SCREEN_TASK);
    CHECK(strcmp(o.text, "B") == 0);
    CHECK(o.value == 1);
    CHECK(o.sound == SOUND_NONE);

    CHECK(!ev(&a, EVT_TASK_DONE, 99, 0, NULL, &o));      /* невідома */
    CHECK(!ev(&a, EVT_TASK_SEEN, 0, 0, NULL, &o));       /* id = 0 */
    CHECK(!ev(&a, EVT_TASK_DUE, 0, 0, "X", &o));
    CHECK(ev(&a, EVT_TASK_DONE, 2, 0, NULL, &o));
    CHECK(o.screen == SCREEN_IDLE);
    CHECK(!ev(&a, EVT_TASK_SEEN, 2, 0, NULL, &o));       /* вдруге вже немає */
}

static void test_priority_matrix(void)
{
    /* 24 комбінації: задача, "Повернись", перерва, {нічого, фокус, банер}.
     * Результат не залежить від порядку подій. */
    for (int pass = 0; pass < 2; pass++) {
        for (int m = 0; m < 24; m++) {
            bool task = m & 1, absence = m & 2, brk = m & 4;
            int fd = m / 8;                              /* 0 нічого, 1 фокус, 2 банер */
            arbiter_t a; ui_cmd_t o;
            arb_init(&a);

            for (int k = 0; k < 4; k++) {
                int step = pass == 0 ? k : 3 - k;
                switch (step) {
                case 0: if (task)    ev(&a, EVT_TASK_DUE, 1, 0, "T", &o); break;
                case 1: if (absence) ev(&a, EVT_ABSENCE_ALERT, 0, 0, NULL, &o); break;
                case 2: if (brk)     ev(&a, EVT_BREAK_DUE, 0, 0, NULL, &o); break;
                default:
                    if (fd >= 1) ev(&a, EVT_FOCUS_START, 1, 5, "F", &o);
                    if (fd == 2) ev(&a, EVT_FOCUS_DONE, 1, 5, "F", &o);
                }
            }
            arb_layer_t want = task ? ARB_LAYER_TASK
                             : absence ? ARB_LAYER_ABSENCE
                             : brk ? ARB_LAYER_BREAK
                             : fd == 1 ? ARB_LAYER_FOCUS
                             : fd == 2 ? ARB_LAYER_DONE
                             : ARB_LAYER_IDLE;
            CHECK(arb_layer(&a) == want);
        }
    }
}

static void test_sync_tasks(void)
{
    arbiter_t a; ui_cmd_t o;
    task_t t[4];
    arb_init(&a);
    ev(&a, EVT_TASK_DUE, 1, 0, "one", &o);
    ev(&a, EVT_TASK_DUE, 2, 0, "two", &o);
    ev(&a, EVT_TASK_DUE, 3, 0, "three", &o);

    memset(t, 0, sizeof(t));
    t[0].id = 1; t[0].status = TASK_DUE;  snprintf(t[0].text, sizeof(t[0].text), "ONE");
    t[1].id = 2; t[1].status = TASK_SEEN;
    t[2].id = 4; t[2].status = TASK_DUE;  snprintf(t[2].text, sizeof(t[2].text), "four");
    /* 3 зникла з розкладу */

    CHECK(arb_sync_tasks(&a, t, 3, &o));                 /* текст першої оновлено */
    CHECK(o.screen == SCREEN_TASK);
    CHECK(strcmp(o.text, "ONE") == 0);
    CHECK(o.value == 2);                                  /* лишились 1 і 4 */
    CHECK(o.sound == SOUND_NONE);
    CHECK(!arb_sync_tasks(&a, t, 3, &o));                 /* без змін */

    t[0].status = TASK_DONE;                              /* 1 більше не DUE */
    CHECK(arb_sync_tasks(&a, t, 3, &o));
    CHECK(strcmp(o.text, "four") == 0);
    CHECK(o.value == 1);

    t[2].status = TASK_SCHEDULED;
    CHECK(arb_sync_tasks(&a, t, 3, &o));
    CHECK(o.screen == SCREEN_IDLE);
    CHECK(arb_layer(&a) == ARB_LAYER_IDLE);

    CHECK(!arb_sync_tasks(&a, NULL, 0, &o));              /* порожній розклад */
    CHECK(!arb_sync_tasks(&a, NULL, 3, &o));              /* некоректні аргументи */

    /* додавання нової DUE через sync (наприклад, після відновлення): без звуку */
    t[3].id = 9; t[3].status = TASK_DUE; snprintf(t[3].text, sizeof(t[3].text), "nine");
    CHECK(arb_sync_tasks(&a, &t[3], 1, &o));
    CHECK(o.screen == SCREEN_TASK);
    CHECK(o.sound == SOUND_NONE);

    /* забагато задач: обрізається, не падає */
    task_t many[FD_MAX_TASKS + 4];
    memset(many, 0, sizeof(many));
    for (int i = 0; i < FD_MAX_TASKS + 4; i++) {
        many[i].id = (uint16_t)(100 + i);
        many[i].status = TASK_DUE;
    }
    arb_sync_tasks(&a, many, FD_MAX_TASKS + 4, &o);
    CHECK(a.due_count == FD_MAX_TASKS);
}

static void test_dups_and_edge_cases(void)
{
    arbiter_t a; ui_cmd_t o;
    arb_init(&a);

    CHECK(ev(&a, EVT_BREAK_DUE, 0, 0, NULL, &o));
    CHECK(!ev(&a, EVT_BREAK_DUE, 0, 0, NULL, &o));       /* повтор не дзвенить */
    ev(&a, EVT_BREAK_DONE, 0, 0, NULL, &o);
    CHECK(ev(&a, EVT_ABSENCE_ALERT, 0, 0, NULL, &o));
    CHECK(!ev(&a, EVT_ABSENCE_ALERT, 0, 0, NULL, &o));
    ev(&a, EVT_PRESENCE_BACK, 0, 0, NULL, &o);

    /* події фокусу без старту ігноруються */
    CHECK(!ev(&a, EVT_FOCUS_PAUSE, 1, 30, NULL, &o));
    CHECK(!ev(&a, EVT_FOCUS_RESUME, 1, 30, NULL, &o));
    CHECK(!ev(&a, EVT_FOCUS_SUSPENDED, 1, 30, NULL, &o));
    CHECK(arb_layer(&a) == ARB_LAYER_IDLE);

    /* некоректна тривалість: без ділення на нуль */
    CHECK(ev(&a, EVT_FOCUS_START, 1, -5, "Z", &o));
    CHECK(o.ring_value == 0);
    CHECK(o.value == 0);
    CHECK(ticks(&a, 3) == 0);                             /* нічого не змінюється */

    /* великий dt: залишок не йде в мінус */
    CHECK(ev(&a, EVT_FOCUS_START, 1, 1, "Z", &o));
    CHECK(arb_tick(&a, 100000, &o));
    CHECK(o.value == 0);
    CHECK(o.ring_value == 100);
    CHECK(ev(&a, EVT_FOCUS_DONE, 1, 1, "Z", &o));
    CHECK(arb_tick(&a, 100000, &o));                       /* банер закінчився */
    CHECK(o.screen == SCREEN_IDLE);

    /* новий старт знімає банер */
    ev(&a, EVT_FOCUS_START, 1, 1, "Q", &o);
    ev(&a, EVT_FOCUS_DONE, 1, 1, "Q", &o);
    CHECK(arb_layer(&a) == ARB_LAYER_DONE);
    CHECK(ev(&a, EVT_FOCUS_START, 2, 5, "W", &o));
    CHECK(o.screen == SCREEN_FOCUS);
    CHECK(o.sound == SOUND_NONE);
    CHECK(o.value == 300);

    /* DONE без тексту бере назву з сесії */
    ev(&a, EVT_FOCUS_DONE, 0, 5, NULL, &o);
    CHECK(strcmp(o.text, "W") == 0);
}

static void test_e9_scenarios(void)
{
    arbiter_t a; ui_cmd_t o;

    /* E9-1: перерва в момент завершення сесії */
    arb_init(&a);
    ev(&a, EVT_FOCUS_START, 1, 1, "Сесія", &o);
    ev(&a, EVT_BREAK_DUE, 0, 0, NULL, &o);
    CHECK(arb_layer(&a) == ARB_LAYER_BREAK);
    CHECK(!ev(&a, EVT_FOCUS_DONE, 1, 1, "Сесія", &o));   /* перерва лишається зверху */
    CHECK(arb_layer(&a) == ARB_LAYER_BREAK);
    CHECK(ev(&a, EVT_BREAK_DONE, 0, 0, NULL, &o));       /* потім банер із сигналом */
    CHECK(o.screen == SCREEN_FOCUS);
    CHECK(o.sound == SOUND_BEEP_DONE);
    CHECK(strcmp(o.text, "Сесія") == 0);

    /* E9-2: задача під час перерви */
    arb_init(&a);
    ev(&a, EVT_BREAK_DUE, 0, 0, NULL, &o);
    CHECK(ev(&a, EVT_TASK_DUE, 1, 0, "Задача", &o));
    CHECK(o.screen == SCREEN_TASK);
    CHECK(ev(&a, EVT_TASK_SEEN, 1, 0, NULL, &o));
    CHECK(o.screen == SCREEN_BREAK);
    CHECK(o.sound == SOUND_NONE);                         /* перерва вже звучала */

    /* E9-3: людина не повернулась після перерви */
    arb_init(&a);
    ev(&a, EVT_BREAK_DUE, 0, 0, NULL, &o);
    ev(&a, EVT_BREAK_DONE, 0, 0, NULL, &o);
    CHECK(arb_layer(&a) == ARB_LAYER_IDLE);
    CHECK(ev(&a, EVT_ABSENCE_ALERT, 0, 0, NULL, &o));
    CHECK(o.screen == SCREEN_ABSENCE_ALERT);
    CHECK(ev(&a, EVT_PRESENCE_BACK, 0, 0, NULL, &o));
    CHECK(o.screen == SCREEN_IDLE);

    /* E9-4: сесія й задача одночасно */
    arb_init(&a);
    ev(&a, EVT_FOCUS_START, 1, 25, "Сесія", &o);
    ticks(&a, 30);
    CHECK(ev(&a, EVT_TASK_DUE, 4, 0, "Задача", &o));
    CHECK(o.screen == SCREEN_TASK);
    ticks(&a, 30);
    CHECK(ev(&a, EVT_TASK_DONE, 4, 0, NULL, &o));         /* "виконано" одразу з DUE */
    CHECK(o.screen == SCREEN_FOCUS);
    CHECK(o.value == 25 * 60 - 60);                       /* час сесії не загубився */
}

/* Випадкові послідовності подій проти простої моделі: які шари активні. */
static uint32_t rng_state = 12345u;
static uint32_t rnd(uint32_t n)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (rng_state >> 8) % n;
}

static bool screen_matches(arb_layer_t l, const ui_cmd_t *o)
{
    switch (l) {
    case ARB_LAYER_TASK:    return o->screen == SCREEN_TASK;
    case ARB_LAYER_ABSENCE: return o->screen == SCREEN_ABSENCE_ALERT;
    case ARB_LAYER_BREAK:   return o->screen == SCREEN_BREAK;
    case ARB_LAYER_FOCUS:   return o->screen == SCREEN_FOCUS ||
                                   o->screen == SCREEN_FOCUS_PAUSED;
    case ARB_LAYER_DONE:    return o->screen == SCREEN_FOCUS;
    default:                return o->screen == SCREEN_IDLE;
    }
}

static bool sound_allowed(arb_layer_t l, sound_t s)
{
    if (s == SOUND_NONE) return true;
    return (l == ARB_LAYER_TASK    && s == SOUND_BEEP_FAST)  ||
           (l == ARB_LAYER_ABSENCE && s == SOUND_BEEP_SLOW)  ||
           (l == ARB_LAYER_BREAK   && s == SOUND_BEEP_SHORT) ||
           (l == ARB_LAYER_DONE    && s == SOUND_BEEP_DONE);
}

static void test_random_against_model(void)
{
    arbiter_t a; ui_cmd_t o;
    bool due[FD_MAX_TASKS + 4] = { false };       /* індекс = id (1..19) */
    int  due_n = 0, banner = 0;
    bool absence = false, brk = false, focus = false;
    int  bad_layer = 0, bad_cmd = 0;
    arb_init(&a);

    for (int step = 0; step < 20000; step++) {
        uint16_t id = (uint16_t)(1 + rnd(19));
        bool emitted = false;
        switch (rnd(13)) {
        case 0:
            emitted = ev(&a, EVT_TASK_DUE, id, 0, "t", &o);
            if (!due[id] && due_n < FD_MAX_TASKS) { due[id] = true; due_n++; }
            break;
        case 1:
            emitted = ev(&a, EVT_TASK_SEEN, id, 0, NULL, &o);
            if (due[id]) { due[id] = false; due_n--; }
            break;
        case 2:
            emitted = ev(&a, EVT_TASK_DONE, id, 0, NULL, &o);
            if (due[id]) { due[id] = false; due_n--; }
            break;
        case 3:  emitted = ev(&a, EVT_ABSENCE_ALERT, 0, 0, NULL, &o); absence = true; break;
        case 4:  emitted = ev(&a, EVT_PRESENCE_BACK, 0, 0, NULL, &o); absence = false; break;
        case 5:  emitted = ev(&a, EVT_BREAK_DUE, 0, 0, NULL, &o); brk = true; break;
        case 6:  emitted = ev(&a, EVT_BREAK_DONE, 0, 0, NULL, &o); brk = false; break;
        case 7:
            emitted = ev(&a, EVT_FOCUS_START, 1, (int32_t)(1 + rnd(30)), "f", &o);
            focus = true; banner = 0;
            break;
        case 8:
        {
            static const event_type_t kinds[3] =
                { EVT_FOCUS_PAUSE, EVT_FOCUS_RESUME, EVT_FOCUS_SUSPENDED };
            emitted = ev(&a, kinds[rnd(3)], 1, (int32_t)rnd(1800), NULL, &o);
        }
            break;
        case 9:
            emitted = ev(&a, EVT_FOCUS_DONE, 1, 5, "f", &o);
            focus = false; banner = ARB_DONE_BANNER_S;
            break;
        default:
            emitted = arb_tick(&a, 1, &o);
            if (banner > 0) banner--;
            break;
        }

        arb_layer_t want = due_n > 0 ? ARB_LAYER_TASK
                         : absence ? ARB_LAYER_ABSENCE
                         : brk ? ARB_LAYER_BREAK
                         : focus ? ARB_LAYER_FOCUS
                         : banner > 0 ? ARB_LAYER_DONE
                         : ARB_LAYER_IDLE;
        arb_layer_t got = arb_layer(&a);
        if (got != want) bad_layer++;
        if (emitted && (!screen_matches(got, &o) || !sound_allowed(got, o.sound)))
            bad_cmd++;
    }
    CHECK(bad_layer == 0);
    CHECK(bad_cmd == 0);
}

static void test_text_and_nulls(void)
{
    arbiter_t a; ui_cmd_t o; event_t e;
    arb_init(&a);

    memset(&e, 0, sizeof(e));
    e.type = EVT_TASK_DUE;
    e.id = 1;
    memset(e.text, 'x', FD_TEXT_MAX);                    /* без '\0' */
    CHECK(arb_handle_event(&a, &e, &o));
    CHECK(strlen(o.text) == FD_TEXT_MAX - 1);

    CHECK(strlen(ARB_TEXT_ABSENCE) < FD_TEXT_MAX);
    CHECK(strlen(ARB_TEXT_BREAK) < FD_TEXT_MAX);

    arb_init(NULL);
    CHECK(!arb_handle_event(NULL, &e, &o));
    CHECK(!arb_handle_event(&a, NULL, &o));
    CHECK(!arb_handle_event(&a, &e, NULL));
    CHECK(!arb_tick(NULL, 1, &o));
    CHECK(!arb_tick(&a, 1, NULL));
    CHECK(!arb_current(NULL, &o));
    CHECK(!arb_current(&a, NULL));
    CHECK(!arb_sync_tasks(NULL, NULL, 0, &o));
    CHECK(arb_layer(NULL) == ARB_LAYER_IDLE);
    CHECK(strcmp(arb_layer_name(ARB_LAYER_TASK), "TASK") == 0);
    CHECK(strcmp(arb_layer_name(ARB_LAYER_ABSENCE), "ABSENCE") == 0);
    CHECK(strcmp(arb_layer_name(ARB_LAYER_IDLE), "IDLE") == 0);
    CHECK(strcmp(arb_layer_name(ARB_LAYER_DONE), "DONE") == 0);
    CHECK(strcmp(arb_layer_name(ARB_LAYER_FOCUS), "FOCUS") == 0);
    CHECK(strcmp(arb_layer_name(ARB_LAYER_BREAK), "BREAK") == 0);
    CHECK(strcmp(arb_layer_name((arb_layer_t)99), "?") == 0);
}

int main(void)
{
    test_idle_and_ignored();
    test_focus_lifecycle();
    test_break_over_focus();
    test_task_over_everything_and_return();
    test_absence_returns_until_back();
    test_hidden_break_sound_replayed();
    test_two_tasks();
    test_priority_matrix();
    test_sync_tasks();
    test_dups_and_edge_cases();
    test_e9_scenarios();
    test_random_against_model();
    test_text_and_nulls();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
