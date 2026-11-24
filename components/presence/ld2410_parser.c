#include "ld2410_parser.h"

#include <string.h>

static const uint8_t HDR[4] = { 0xF4, 0xF3, 0xF2, 0xF1 };
static const uint8_t FTR[4] = { 0xF8, 0xF7, 0xF6, 0xF5 };

#define DATA_MIN   13u      /* тип, 0xAA, 9 байт цілі, 0x55, 0x00 */

void ld2410_init(ld2410_parser_t *p)
{
    if (!p) return;
    memset(p, 0, sizeof(*p));
}

static void resync(ld2410_parser_t *p)
{
    p->len = 0;
    p->expect = 0;
}

static uint16_t le16(const uint8_t *b)
{
    return (uint16_t)(b[0] | (b[1] << 8));
}

/* Перевірка й розбір повного кадру в p->buf. */
static bool decode(const ld2410_parser_t *p, ld2410_frame_t *out)
{
    const size_t   total = p->expect;
    const uint8_t *d     = p->buf + 6;                  /* початок даних */
    const size_t   dlen  = total - 10;

    if (memcmp(p->buf + total - 4, FTR, 4) != 0) return false;
    if (d[0] != 0x01 && d[0] != 0x02) return false;     /* тип кадру */
    if (d[1] != 0xAA) return false;
    if (d[dlen - 2] != 0x55 || d[dlen - 1] != 0x00) return false;
    if (d[2] > 3) return false;                          /* стан цілі */

    if (out) {
        out->engineering   = (d[0] == 0x01);
        out->target_state  = d[2];
        out->moving_cm     = le16(&d[3]);
        out->moving_energy = d[5];
        out->still_cm      = le16(&d[6]);
        out->still_energy  = d[8];
        out->detect_cm     = le16(&d[9]);
    }
    return true;
}

bool ld2410_feed(ld2410_parser_t *p, uint8_t byte, ld2410_frame_t *out)
{
    if (!p) return false;

    /* 1. Пошук заголовка. */
    if (p->len < sizeof(HDR)) {
        if (byte == HDR[p->len]) {
            p->buf[p->len++] = byte;
        } else {
            p->bytes_skipped += p->len + (byte == HDR[0] ? 0u : 1u);
            p->len = 0;
            if (byte == HDR[0]) p->buf[p->len++] = byte;   /* можливий початок нового */
        }
        return false;
    }

    /* 2. Збір кадру. */
    p->buf[p->len++] = byte;

    if (p->len == 6) {
        size_t dlen = le16(&p->buf[4]);
        if (dlen < DATA_MIN || 10 + dlen > LD2410_MAX_FRAME) {
            p->frames_bad++;
            resync(p);
            return false;
        }
        p->expect = 10 + dlen;
        return false;
    }

    if (p->expect != 0 && p->len == p->expect) {
        bool ok = decode(p, out);
        if (ok) p->frames_ok++; else p->frames_bad++;
        resync(p);
        return ok;
    }
    return false;
}
