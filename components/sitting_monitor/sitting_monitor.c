#include "sitting_monitor.h"
#include <string.h>

static void make_event(event_t *out, event_type_t t, fd_time_us_t ts, int32_t value)
{
    memset(out, 0, sizeof(*out));
    out->type  = t;
    out->ts_us = ts;
    out->value = value;
}

void sm_init(sitting_monitor_t *sm, const config_t *cfg)
{
    if (!sm) return;
    memset(sm, 0, sizeof(*sm));
    sm->cfg   = cfg ? *cfg : fd_config_default();
    sm->state = SM_ABSENT;
}

void sm_handle_event(sitting_monitor_t *sm, const event_t *in)
{
    if (!sm || !in) return;

    switch (in->type) {
    case EVT_PRESENCE_LOST:
        if (sm->state == SM_SITTING || sm->state == SM_BREAK_DUE) {
            sm->break_was_due = (sm->state == SM_BREAK_DUE);
            sm->away_s = 0;
            sm->tracking_absence = true;
            sm->alert_sent = false;
            sm->state  = SM_AWAY;
        }
        break;

    case EVT_PRESENCE_BACK:
        if (sm->state == SM_ABSENT) {
            sm->sit_s = 0;
            sm->away_s = 0;
            sm->tracking_absence = false;
            sm->alert_sent = false;
            sm->state = SM_SITTING;
        } else if (sm->state == SM_AWAY) {
            /* коротка відсутність: лічильник збережено */
            sm->state = sm->break_was_due ? SM_BREAK_DUE : SM_SITTING;
            sm->away_s = 0;
            sm->tracking_absence = false;
            sm->break_was_due = false;
        }
        break;

    default:
        break;
    }
}

bool sm_tick(sitting_monitor_t *sm, uint32_t dt_s, fd_time_us_t now_us,
             event_t *out)
{
    if (!sm || !out || dt_s == 0) return false;

    const uint32_t threshold_s = sm->cfg.sit_threshold_min * 60u;
    const uint32_t reset_s     = sm->cfg.break_reset_min * 60u;
    const uint32_t alert_s     = sm->cfg.absence_alert_min * 60u;

    switch (sm->state) {
    case SM_SITTING:
        sm->sit_s += dt_s;
        if (sm->sit_s >= threshold_s) {
            sm->state = SM_BREAK_DUE;
            make_event(out, EVT_BREAK_DUE, now_us, (int32_t)(sm->sit_s / 60u));
            return true;
        }
        break;

    case SM_BREAK_DUE:
        sm->sit_s += dt_s;      /* лічильник росте далі, нових подій немає */
        break;

    case SM_AWAY:
        sm->away_s += dt_s;
        if (sm->away_s >= reset_s) {
            int32_t away_min = (int32_t)(sm->away_s / 60u);
            sm->sit_s = 0;
            sm->break_was_due = false;
            /* away_s НЕ скидаємо: відлік "Повернись" іде від початку відсутності */
            sm->state = SM_ABSENT;
            make_event(out, EVT_BREAK_DONE, now_us, away_min);
            return true;
        }
        break;

    case SM_ABSENT:
        if (sm->tracking_absence) {
            sm->away_s += dt_s;
            if (!sm->alert_sent && sm->away_s >= alert_s) {
                sm->alert_sent = true;
                make_event(out, EVT_ABSENCE_ALERT, now_us,
                           (int32_t)(sm->away_s / 60u));
                return true;
            }
        }
        break;

    default:
        break;
    }
    return false;
}

sm_state_t sm_state(const sitting_monitor_t *sm)
{
    return sm ? sm->state : SM_ABSENT;
}

uint32_t sm_sit_seconds(const sitting_monitor_t *sm)
{
    return sm ? sm->sit_s : 0;
}

const char *sm_state_name(sm_state_t s)
{
    switch (s) {
    case SM_ABSENT:    return "ABSENT";
    case SM_SITTING:   return "SITTING";
    case SM_AWAY:      return "AWAY";
    case SM_BREAK_DUE: return "BREAK_DUE";
    default:           return "?";
    }
}
