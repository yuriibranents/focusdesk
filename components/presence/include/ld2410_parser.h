/*
 * ld2410_parser — розбір кадрів даних радара LD2410C (B3).
 * ЧИСТИЙ C: без ESP-IDF, тестується на ПК. Байти подаються по одному.
 *
 * Кадр: F4 F3 F2 F1 | довжина (2 байти, LE) | дані | F8 F7 F6 F5
 * Дані: тип (0x02 базовий, 0x01 інженерний), 0xAA, стан цілі, відстані й
 *       енергії, ..., 0x55, 0x00 (хвіст і перевірка).
 * Кадри-відповіді на команди (заголовок FD FC FB FA) ігноруються.
 * Сміття між кадрами пропускається; після пошкодженого кадру парсер
 * синхронізується з наступним заголовком.
 */
#ifndef LD2410_PARSER_H
#define LD2410_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LD2410_MAX_FRAME   128      /* заголовок + довжина + дані + кінець */

typedef struct {
    uint8_t  target_state;   /* 0 немає, 1 рух, 2 нерухома, 3 рух і нерухома */
    uint16_t moving_cm;
    uint8_t  moving_energy;
    uint16_t still_cm;
    uint8_t  still_energy;
    uint16_t detect_cm;
    bool     engineering;    /* кадр інженерного режиму */
} ld2410_frame_t;

typedef struct {
    uint8_t  buf[LD2410_MAX_FRAME];
    size_t   len;            /* скільки байт кадру вже зібрано */
    size_t   expect;         /* повна довжина кадру, коли відома (len >= 6) */
    uint32_t frames_ok;      /* коректні кадри даних */
    uint32_t frames_bad;     /* пошкоджені кадри (довжина, кінець, хвіст) */
    uint32_t bytes_skipped;  /* байти поза кадрами */
} ld2410_parser_t;

void ld2410_init(ld2410_parser_t *p);

/* Подати один байт. true: завершено коректний кадр даних, він у *out. */
bool ld2410_feed(ld2410_parser_t *p, uint8_t byte, ld2410_frame_t *out);

/* Є ціль (рух або нерухома). */
static inline bool ld2410_present(const ld2410_frame_t *f)
{
    return f && f->target_state != 0;
}

#ifdef __cplusplus
}
#endif
#endif /* LD2410_PARSER_H */
