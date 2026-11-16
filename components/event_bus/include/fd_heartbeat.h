/*
 * fd_heartbeat — перевірка "живості" задач (B2, watchdog).
 * ЧИСТИЙ C (лише stdatomic): без ESP-IDF, тестується на ПК.
 *
 * Кожна задача раз на цикл викликає fd_hb_beat(&hb, FD_HB_xxx, now_ms).
 * Monitor Task раз на секунду викликає fd_hb_stale(): якщо повернуто
 * ненульову маску, ці задачі не відзвітувалися довше за timeout_ms.
 * Час now_ms: мілісекунди від старту (uint32, перехід через 0 враховано).
 */
#ifndef FD_HEARTBEAT_H
#define FD_HEARTBEAT_H

#include <stdatomic.h>
#include "focusdesk_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FD_HB_SLOTS 4
_Static_assert(FD_HB_ALL == 0xFu, "FD_HB_ALL must be bits 0..3");

typedef struct {
    _Atomic uint32_t last_ms[FD_HB_SLOTS];
    uint32_t         timeout_ms;
} fd_hb_t;

/* timeout_ms == 0 -> FD_HB_TIMEOUT_MS. Усі задачі вважаються живими на now_ms. */
void fd_hb_init(fd_hb_t *hb, uint32_t timeout_ms, uint32_t now_ms);

/* bits: одна або кілька констант FD_HB_*; невідомі біти ігноруються. */
void fd_hb_beat(fd_hb_t *hb, uint32_t bits, uint32_t now_ms);

/* Маска FD_HB_* задач, що мовчать >= timeout_ms. 0 = усі живі. */
uint32_t fd_hb_stale(const fd_hb_t *hb, uint32_t now_ms);


/* ---- Рішення watchdog (Monitor Task викликає раз на секунду) ---- */
#define FD_WD_GRACE_MS 5000   /* скільки чекати після першого виявлення */

typedef enum {
    FD_WD_OK = 0,      /* усі живі */
    FD_WD_STALE_NEW,   /* мовчуни виявлені вперше: залогувати, подати EVT_SYSTEM_ERROR */
    FD_WD_STALE_WAIT,  /* мовчать, пільговий час ще не вийшов */
    FD_WD_RESTART      /* мовчать довше за grace_ms: перезавантажити пристрій */
} fd_wd_action_t;

typedef struct {
    bool     active;     /* триває "епізод" мовчання */
    uint32_t since_ms;   /* коли епізод помічено */
    uint32_t grace_ms;
} fd_wd_t;

/* grace_ms == 0 -> FD_WD_GRACE_MS. */
void           fd_wd_init(fd_wd_t *wd, uint32_t grace_ms);
/* stale_mask = результат fd_hb_stale(). Якщо всі ожили, епізод скидається. */
fd_wd_action_t fd_wd_step(fd_wd_t *wd, uint32_t stale_mask, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif /* FD_HEARTBEAT_H */
