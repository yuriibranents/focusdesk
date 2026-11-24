#include "presence_filter.h"

#include <string.h>

static void make_event(event_t *out, event_type_t type, uint32_t now_ms,
                       int32_t value)
{
    memset(out, 0, sizeof(*out));
    out->type  = type;
    out->ts_us = (fd_time_us_t)now_ms * 1000;
    out->value = value;
}

void pf_init(presence_filter_t *pf, const config_t *cfg,
             uint32_t sensor_timeout_ms, uint32_t now_ms)
{
    if (!pf) return;
    config_t c = cfg ? *cfg : fd_config_default();
    memset(pf, 0, sizeof(*pf));
    pf->lost_debounce_ms  = c.presence_lost_debounce_s * 1000u;
    pf->back_debounce_ms  = c.presence_back_debounce_s * 1000u;
    pf->sensor_timeout_ms = sensor_timeout_ms;
    pf->sensor_ok         = true;
    pf->last_frame_ms     = now_ms;       /* відлік мовчання від старту */
}

bool pf_sample(presence_filter_t *pf, bool raw_present, uint32_t now_ms,
               event_t *out)
{
    if (!pf || !out) return false;

    if (!pf->sensor_ok) {
        pf->sensor_ok = true;             /* датчик ожив: дебаунс заново */
        pf->opposite_active = false;
    }
    pf->last_frame_ms = now_ms;

    if (raw_present == pf->present) {
        pf->opposite_active = false;      /* сира величина збігається зі станом */
        return false;
    }

    if (!pf->opposite_active) {
        pf->opposite_active = true;
        pf->opposite_since_ms = now_ms;
    }
    const uint32_t need = raw_present ? pf->back_debounce_ms
                                      : pf->lost_debounce_ms;
    if ((uint32_t)(now_ms - pf->opposite_since_ms) < need) return false;

    pf->present = raw_present;
    pf->opposite_active = false;
    make_event(out, raw_present ? EVT_PRESENCE_BACK : EVT_PRESENCE_LOST,
               now_ms, 0);
    return true;
}

bool pf_tick(presence_filter_t *pf, uint32_t now_ms, event_t *out)
{
    if (!pf || !out) return false;
    if (!pf->sensor_ok) return false;
    if ((uint32_t)(now_ms - pf->last_frame_ms) < pf->sensor_timeout_ms)
        return false;

    pf->sensor_ok = false;
    pf->opposite_active = false;          /* мовчання не рахується як відсутність */
    make_event(out, EVT_SYSTEM_ERROR, now_ms, (int32_t)FD_ERR_SENSOR);
    return true;
}

bool pf_present(const presence_filter_t *pf)
{
    return pf && pf->present;
}

bool pf_sensor_ok(const presence_filter_t *pf)
{
    return pf && pf->sensor_ok;
}
