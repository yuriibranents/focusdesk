#include "fd_heartbeat.h"

void fd_hb_init(fd_hb_t *hb, uint32_t timeout_ms, uint32_t now_ms)
{
    if (!hb) return;
    hb->timeout_ms = timeout_ms ? timeout_ms : FD_HB_TIMEOUT_MS;
    for (int i = 0; i < FD_HB_SLOTS; i++)
        atomic_store(&hb->last_ms[i], now_ms);
}

void fd_hb_beat(fd_hb_t *hb, uint32_t bits, uint32_t now_ms)
{
    if (!hb) return;
    for (int i = 0; i < FD_HB_SLOTS; i++)
        if (bits & (1u << i))
            atomic_store(&hb->last_ms[i], now_ms);
}

uint32_t fd_hb_stale(const fd_hb_t *hb, uint32_t now_ms)
{
    uint32_t mask = 0;
    if (!hb) return 0;
    for (int i = 0; i < FD_HB_SLOTS; i++) {
        uint32_t last = atomic_load(&hb->last_ms[i]);
        if ((uint32_t)(now_ms - last) >= hb->timeout_ms)
            mask |= (1u << i);
    }
    return mask;
}
