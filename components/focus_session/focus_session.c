#include "focus_session.h"
#include <string.h>

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    size_t i = 0;
    if (dst_size == 0) return;
    if (src) {
        for (; i + 1 < dst_size && src[i] != '\0'; i++) dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void make_event(event_t *out, const focus_session_t *fs,
                       event_type_t t, fd_time_us_t ts, int32_t value)
{
    memset(out, 0, sizeof(*out));
    out->type  = t;
    out->ts_us = ts;
    out->id    = fs->goal.id;
    out->value = value;
    copy_text(out->text, sizeof(out->text), fs->goal.title);
}

void fs_init(focus_session_t *fs, const config_t *cfg)
{
    if (!fs) return;
    memset(fs, 0, sizeof(*fs));
    fs->cfg   = cfg ? *cfg : fd_config_default();
    fs->state = FS_IDLE;
}

bool fs_select_goal(focus_session_t *fs, const goal_t *goal)
{
    if (!fs || !goal || goal->minutes == 0) return false;
    if (fs->state != FS_IDLE && fs->state != FS_READY && fs->state != FS_DONE)
        return false;

    fs->goal = *goal;
    fs->goal.title[sizeof(fs->goal.title) - 1] = '\0';
    fs->total_s     = (uint32_t)goal->minutes * 60u;
    fs->remaining_s = fs->total_s;
    fs->paused_s    = 0;
    fs->away_s      = 0;
    fs->state       = FS_READY;
    return true;
}

bool fs_cancel(focus_session_t *fs)
{
    if (!fs || fs->state == FS_IDLE) return false;
    memset(&fs->goal, 0, sizeof(fs->goal));
    fs->total_s = fs->remaining_s = fs->paused_s = fs->away_s = 0;
    fs->state = FS_IDLE;
    return true;
}

bool fs_handle_event(focus_session_t *fs, const event_t *in, event_t *out)
{
    if (!fs || !in || !out) return false;

    switch (in->type) {
    case EVT_FOCUS_START:
        if (fs->state == FS_READY) {
            fs->state = FS_RUNNING;
            make_event(out, fs, EVT_FOCUS_START, in->ts_us,
                       (int32_t)(fs->total_s / 60u));
            return true;
        }
        if (fs->state == FS_SUSPENDED) {
            fs->state  = FS_RUNNING;
            fs->away_s = 0;
            make_event(out, fs, EVT_FOCUS_RESUME, in->ts_us,
                       (int32_t)fs->remaining_s);
            return true;
        }
        break;

    case EVT_PRESENCE_LOST:
        if (fs->state == FS_RUNNING) {
            fs->state  = FS_PAUSED;
            fs->away_s = 0;
            make_event(out, fs, EVT_FOCUS_PAUSE, in->ts_us,
                       (int32_t)fs->remaining_s);
            return true;
        }
        break;

    case EVT_PRESENCE_BACK:
        if (fs->state == FS_PAUSED) {
            fs->state  = FS_RUNNING;
            fs->away_s = 0;
            make_event(out, fs, EVT_FOCUS_RESUME, in->ts_us,
                       (int32_t)fs->remaining_s);
            return true;
        }
        break;

    default:
        break;
    }
    return false;
}

bool fs_tick(focus_session_t *fs, uint32_t dt_s, fd_time_us_t now_us,
             event_t *out)
{
    if (!fs || !out || dt_s == 0) return false;

    const uint32_t suspend_s = fs->cfg.suspend_after_min * 60u;

    switch (fs->state) {
    case FS_RUNNING:
        if (dt_s >= fs->remaining_s) {
            fs->remaining_s = 0;
            fs->state = FS_DONE;
            make_event(out, fs, EVT_FOCUS_DONE, now_us,
                       (int32_t)(fs->total_s / 60u));
            return true;
        }
        fs->remaining_s -= dt_s;
        break;

    case FS_PAUSED:
        fs->paused_s += dt_s;
        fs->away_s   += dt_s;
        if (fs->away_s >= suspend_s) {
            fs->state = FS_SUSPENDED;
            make_event(out, fs, EVT_FOCUS_SUSPENDED, now_us,
                       (int32_t)fs->remaining_s);
            return true;
        }
        break;

    case FS_SUSPENDED:
        fs->paused_s += dt_s;
        break;

    case FS_DONE:
        fs->state = FS_IDLE;
        break;

    case FS_IDLE:
    case FS_READY:
    default:
        break;
    }
    return false;
}

fs_state_t fs_state(const focus_session_t *fs)
{
    return fs ? fs->state : FS_IDLE;
}

uint32_t fs_remaining_seconds(const focus_session_t *fs)
{
    return fs ? fs->remaining_s : 0;
}

uint32_t fs_paused_seconds(const focus_session_t *fs)
{
    return fs ? fs->paused_s : 0;
}

const goal_t *fs_goal(const focus_session_t *fs)
{
    return fs ? &fs->goal : NULL;
}

const char *fs_state_name(fs_state_t s)
{
    switch (s) {
    case FS_IDLE:      return "IDLE";
    case FS_READY:     return "READY";
    case FS_RUNNING:   return "RUNNING";
    case FS_PAUSED:    return "PAUSED";
    case FS_SUSPENDED: return "SUSPENDED";
    case FS_DONE:      return "DONE";
    default:           return "?";
    }
}
