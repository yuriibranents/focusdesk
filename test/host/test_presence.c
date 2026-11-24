/* Host-тести ld2410_parser і presence_filter (без ESP-IDF). */
#include "ld2410_parser.h"
#include "presence_filter.h"
#include <stdio.h>
#include <string.h>

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* Будує базовий кадр даних. Повертає довжину. */
static size_t build(uint8_t *b, uint8_t type, uint8_t state, uint16_t mv,
                    uint8_t me, uint16_t st, uint8_t se, uint16_t det)
{
    size_t i = 0;
    b[i++]=0xF4; b[i++]=0xF3; b[i++]=0xF2; b[i++]=0xF1;
    b[i++]=13; b[i++]=0;
    b[i++]=type; b[i++]=0xAA; b[i++]=state;
    b[i++]=mv & 0xFF; b[i++]=mv >> 8; b[i++]=me;
    b[i++]=st & 0xFF; b[i++]=st >> 8; b[i++]=se;
    b[i++]=det & 0xFF; b[i++]=det >> 8;
    b[i++]=0x55; b[i++]=0x00;
    b[i++]=0xF8; b[i++]=0xF7; b[i++]=0xF6; b[i++]=0xF5;
    return i;
}

static int feed_all(ld2410_parser_t *p, const uint8_t *b, size_t n,
                    ld2410_frame_t *last)
{
    int frames = 0;
    for (size_t i = 0; i < n; i++)
        if (ld2410_feed(p, b[i], last)) frames++;
    return frames;
}

static void test_parser(void)
{
    ld2410_parser_t p; ld2410_frame_t f; uint8_t b[64];
    ld2410_init(&p);

    size_t n = build(b, 0x02, 1, 150, 40, 0, 0, 150);
    CHECK(feed_all(&p, b, n, &f) == 1);
    CHECK(f.target_state == 1 && f.moving_cm == 150 && f.moving_energy == 40);
    CHECK(f.detect_cm == 150 && !f.engineering && ld2410_present(&f));

    n = build(b, 0x01, 2, 0, 0, 300, 55, 300);
    CHECK(feed_all(&p, b, n, &f) == 1);
    CHECK(f.engineering && f.still_cm == 300 && f.still_energy == 55);

    n = build(b, 0x02, 0, 0, 0, 0, 0, 0);
    CHECK(feed_all(&p, b, n, &f) == 1 && !ld2410_present(&f));
    CHECK(p.frames_ok == 3 && p.frames_bad == 0);

    /* сміття перед кадром і між кадрами */
    ld2410_init(&p);
    uint8_t g[100]; size_t m = 0;
    g[m++]=0x00; g[m++]=0xF4; g[m++]=0xF4; g[m++]=0xF3; g[m++]=0x12;
    n = build(b, 0x02, 3, 100, 10, 120, 20, 110);
    memcpy(g + m, b, n); m += n;
    g[m++]=0x77;
    CHECK(feed_all(&p, g, m, &f) == 1 && f.target_state == 3);
    CHECK(p.bytes_skipped > 0);

    /* кадр, розбитий на побайтні шматки — той самий результат (feed по одному) */
    ld2410_init(&p);
    n = build(b, 0x02, 1, 500, 80, 0, 0, 500);
    int fr = 0;
    for (size_t i = 0; i < n; i++) if (ld2410_feed(&p, b[i], &f)) fr++;
    CHECK(fr == 1 && f.moving_cm == 500);

    /* пошкоджений хвіст/кінець */
    ld2410_init(&p);
    n = build(b, 0x02, 1, 1, 1, 1, 1, 1); b[n-1] = 0x00;
    CHECK(feed_all(&p, b, n, &f) == 0 && p.frames_bad == 1);
    n = build(b, 0x02, 1, 1, 1, 1, 1, 1); b[17] = 0x56;
    CHECK(feed_all(&p, b, n, &f) == 0 && p.frames_bad == 2);
    n = build(b, 0x02, 9, 1, 1, 1, 1, 1);          /* неможливий стан цілі */
    CHECK(feed_all(&p, b, n, &f) == 0 && p.frames_bad == 3);
    /* після пошкодженого кадру наступний нормальний проходить */
    n = build(b, 0x02, 2, 0, 0, 90, 30, 90);
    CHECK(feed_all(&p, b, n, &f) == 1 && f.still_cm == 90);

    /* некоректна довжина: надто мала й надто велика */
    ld2410_init(&p);
    uint8_t bad1[] = {0xF4,0xF3,0xF2,0xF1, 0x02,0x00, 0x01,0x02};
    CHECK(feed_all(&p, bad1, sizeof bad1, &f) == 0 && p.frames_bad == 1);
    uint8_t bad2[] = {0xF4,0xF3,0xF2,0xF1, 0xFF,0xFF};
    CHECK(feed_all(&p, bad2, sizeof bad2, &f) == 0 && p.frames_bad == 2);
    n = build(b, 0x02, 1, 10, 10, 0, 0, 10);
    CHECK(feed_all(&p, b, n, &f) == 1);

    /* кадр-відповідь на команду (інший заголовок) ігнорується */
    ld2410_init(&p);
    uint8_t ack[] = {0xFD,0xFC,0xFB,0xFA, 0x04,0x00, 0xFF,0x01,0x00,0x00, 0x04,0x03,0x02,0x01};
    CHECK(feed_all(&p, ack, sizeof ack, &f) == 0 && p.frames_ok == 0);

    /* NULL-безпека */
    ld2410_init(NULL);
    CHECK(!ld2410_feed(NULL, 0, &f));
    CHECK(!ld2410_present(NULL));

    /* фазз: випадкові байти не ламають (ASan) і не дають хибних кадрів */
    ld2410_init(&p);
    uint32_t s = 12345;
    for (int i = 0; i < 200000; i++) {
        s = s * 1664525u + 1013904223u;
        ld2410_feed(&p, (uint8_t)(s >> 24), &f);
    }
    CHECK(p.frames_ok < 3);
}

static bool samp(presence_filter_t *pf, bool raw, uint32_t t, event_type_t *ty)
{
    event_t e;
    if (pf_sample(pf, raw, t, &e)) { *ty = e.type; return true; }
    return false;
}

static void test_filter(void)
{
    presence_filter_t pf; event_type_t ty = EVT__COUNT; event_t e;
    pf_init(&pf, NULL, 3000, 0);
    CHECK(!pf_present(&pf) && pf_sensor_ok(&pf));

    /* у порожній кімнаті подій немає */
    for (uint32_t t = 0; t < 30000; t += 100) CHECK(!samp(&pf, false, t, &ty));

    /* BACK рівно через 2 с безперервної присутності */
    uint32_t t0 = 30000; bool got = false; uint32_t at = 0;
    for (uint32_t t = t0; t <= t0 + 3000; t += 100)
        if (samp(&pf, true, t, &ty)) { got = true; at = t - t0; break; }
    CHECK(got && ty == EVT_PRESENCE_BACK && at == 2000 && pf_present(&pf));

    /* коротке зникнення (<10 с) не дає LOST */
    uint32_t t1 = t0 + 3100;
    for (uint32_t t = t1; t < t1 + 9900; t += 100) CHECK(!samp(&pf, false, t, &ty));
    CHECK(!samp(&pf, true, t1 + 9900, &ty) && pf_present(&pf));

    /* а після повернення відлік починається заново */
    uint32_t t2 = t1 + 10000; got = false;
    for (uint32_t t = t2; t <= t2 + 11000; t += 100)
        if (samp(&pf, false, t, &ty)) { got = true; at = t - t2; break; }
    CHECK(got && ty == EVT_PRESENCE_LOST && at == 10000 && !pf_present(&pf));

    /* блимання під 2 с не дає BACK */
    uint32_t t3 = t2 + 12000;
    for (int i = 0; i < 50; i++) {
        CHECK(!samp(&pf, true, t3, &ty)); t3 += 1500;
        CHECK(!samp(&pf, false, t3, &ty)); t3 += 100;
    }
    CHECK(!pf_present(&pf));

    /* час переходить через межу uint32 */
    presence_filter_t pw; pf_init(&pw, NULL, 3000, 0xFFFFF000u);
    uint32_t tw = 0xFFFFF000u; got = false;
    for (int i = 0; i < 40; i++, tw += 100)
        if (samp(&pw, true, tw, &ty)) { got = true; break; }
    CHECK(got && ty == EVT_PRESENCE_BACK);

    /* fail-safe: датчик замовк */
    presence_filter_t pg; pf_init(&pg, NULL, 3000, 0);
    CHECK(!pf_tick(&pg, 2999, &e));
    CHECK(pf_tick(&pg, 3000, &e) && e.type == EVT_SYSTEM_ERROR &&
          e.value == (int32_t)FD_ERR_SENSOR && !pf_sensor_ok(&pg));
    CHECK(!pf_tick(&pg, 9000, &e));              /* лише один раз */

    /* під час мовчання LOST немає, дебаунс скинуто */
    presence_filter_t ph; pf_init(&ph, NULL, 3000, 0);
    uint32_t th = 0; got = false;
    for (; th <= 2100; th += 100) samp(&ph, true, th, &ty);       /* BACK */
    CHECK(pf_present(&ph));
    for (uint32_t t = th; t < th + 5000; t += 100) samp(&ph, false, t, &ty);
    /* 5 с відсутності зафіксовано, потім мовчання на 60 с */
    uint32_t tq = th + 5000;
    CHECK(pf_tick(&ph, tq + 3000, &e));
    CHECK(!pf_tick(&ph, tq + 60000, &e) && pf_present(&ph));
    /* кадри повернулися з «немає людини»: відлік 10 с стартує заново */
    uint32_t tr = tq + 61000; got = false;
    for (uint32_t t = tr; t <= tr + 11000; t += 100)
        if (samp(&ph, false, t, &ty)) { got = true; at = t - tr; break; }
    CHECK(pf_sensor_ok(&ph) && got && ty == EVT_PRESENCE_LOST && at == 10000);

    /* кастомні налаштування */
    config_t c = fd_config_default();
    c.presence_lost_debounce_s = 1; c.presence_back_debounce_s = 1;
    presence_filter_t pc; pf_init(&pc, &c, 3000, 0);
    got = false;
    for (uint32_t t = 0; t <= 1500; t += 100)
        if (samp(&pc, true, t, &ty)) { got = true; at = t; break; }
    CHECK(got && at == 1000);

    CHECK(!pf_sample(NULL, true, 0, &e) && !pf_tick(NULL, 0, &e));
    CHECK(!pf_present(NULL) && !pf_sensor_ok(NULL));
}

int main(void)
{
    test_parser();
    test_filter();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
