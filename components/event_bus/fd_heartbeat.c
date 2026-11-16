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

void fd_wd_init(fd_wd_t *wd, uint32_t grace_ms)
{
    if (!wd) return;
    wd->active   = false;
    wd->since_ms = 0;
    wd->grace_ms = grace_ms ? grace_ms : FD_WD_GRACE_MS;
}

fd_wd_action_t fd_wd_step(fd_wd_t *wd, uint32_t stale_mask, uint32_t now_ms)
{
    if (!wd) return FD_WD_OK;

    if (stale_mask == 0) {
        wd->active = false;
        return FD_WD_OK;
    }
    if (!wd->active) {
        wd->active   = true;
        wd->since_ms = now_ms;
        return FD_WD_STALE_NEW;
    }
    if ((uint32_t)(now_ms - wd->since_ms) >= wd->grace_ms)
        return FD_WD_RESTART;
    return FD_WD_STALE_WAIT;
}
