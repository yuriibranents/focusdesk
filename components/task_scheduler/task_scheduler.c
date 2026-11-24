#include "task_scheduler.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Допоміжні функції                                                   */
/* ------------------------------------------------------------------ */

static uint32_t sat_add_u32(uint32_t a, uint32_t b)
{
    return (a > UINT32_MAX - b) ? UINT32_MAX : a + b;
}

static void copy_text(char *dst, const char *src)
{
    memcpy(dst, src, FD_TEXT_MAX);
    dst[FD_TEXT_MAX - 1] = '\0';        /* текст завжди завершений нулем */
}

static int find_idx(const task_scheduler_t *ts, uint16_t id)
{
    for (int i = 0; i < ts->sched.task_count; i++)
        if (ts->sched.tasks[i].id == id) return i;
    return -1;
}

static int first_with_status(const task_scheduler_t *ts, task_status_t st)
{
    for (int i = 0; i < ts->sched.task_count; i++)
        if (ts->sched.tasks[i].status == st) return i;
    return -1;
}

/* Перевірка розкладу; status_check — перевіряти допустимість полів status. */
static bool schedule_valid(const schedule_t *s, bool status_check)
{
    if (s->goal_count > FD_MAX_GOALS) return false;
    if (s->task_count > FD_MAX_TASKS) return false;
    for (int i = 0; i < s->task_count; i++) {
        if (s->tasks[i].id == 0) return false;
        if (status_check && (unsigned)s->tasks[i].status > (unsigned)TASK_DONE)
            return false;
        for (int j = 0; j < i; j++)
            if (s->tasks[j].id == s->tasks[i].id) return false;
    }
    return true;
}

static void terminate_texts(schedule_t *s)
{
    for (int i = 0; i < FD_MAX_GOALS; i++)
        s->goals[i].title[FD_TEXT_MAX - 1] = '\0';
    for (int i = 0; i < FD_MAX_TASKS; i++)
        s->tasks[i].text[FD_TEXT_MAX - 1] = '\0';
}

static void make_event(event_t *out, event_type_t type, fd_time_us_t ts_us,
                       uint16_t id, int32_t value, const char *text)
{
    memset(out, 0, sizeof(*out));
    out->type  = type;
    out->ts_us = ts_us;
    out->id    = id;
    out->value = value;
    if (text) copy_text(out->text, text);
}

static void make_due_event(task_scheduler_t *ts, int i, fd_time_us_t now_us,
                           event_t *out)
{
    make_event(out, EVT_TASK_DUE, now_us, ts->sched.tasks[i].id,
               (int32_t)ts->sent[i], ts->sched.tasks[i].text);
    if (ts->sent[i] < UINT16_MAX) ts->sent[i]++;
    ts->since_s[i] = 0;
    ts->notify[i]  = false;
}

/* ------------------------------------------------------------------ */
/* Публічний API                                                       */
/* ------------------------------------------------------------------ */

void ts_init(task_scheduler_t *ts, const config_t *cfg)
{
    if (!ts) return;
    memset(ts, 0, sizeof(*ts));
    ts->cfg = cfg ? *cfg : fd_config_default();
}

fd_err_t ts_set_schedule(task_scheduler_t *ts, const schedule_t *s,
                         fd_time_us_t now_us, event_t *out)
{
    if (!ts || !s || s == &ts->sched) return FD_ERR_INVALID_ARG;
    if (!schedule_valid(s, false)) return FD_ERR_INVALID_ARG;
    if (ts->has_schedule && s->schedule_version <= ts->sched.schedule_version)
        return FD_ERR_STATE;

    /* Запам'ятовуємо стан старих задач, щоб перенести його в новий розклад. */
    struct {
        uint16_t      id;
        fd_unix_t     time_utc;
        task_status_t status;
        uint32_t      since_s;
        uint16_t      sent;
        bool          notify;
    } old[FD_MAX_TASKS];
    uint8_t old_count = ts->has_schedule ? ts->sched.task_count : 0;
    for (int i = 0; i < old_count; i++) {
        old[i].id       = ts->sched.tasks[i].id;
        old[i].time_utc = ts->sched.tasks[i].time_utc;
        old[i].status   = ts->sched.tasks[i].status;
        old[i].since_s  = ts->since_s[i];
        old[i].sent     = ts->sent[i];
        old[i].notify   = ts->notify[i];
    }

    ts->sched = *s;
    terminate_texts(&ts->sched);
    memset(ts->since_s, 0, sizeof(ts->since_s));
    memset(ts->sent, 0, sizeof(ts->sent));
    memset(ts->notify, 0, sizeof(ts->notify));

    for (int i = 0; i < ts->sched.task_count; i++) {
        task_t *t = &ts->sched.tasks[i];
        t->status = TASK_SCHEDULED;                 /* вхідне поле status ігноруємо */
        for (int j = 0; j < old_count; j++) {
            if (old[j].id != t->id) continue;
            if (old[j].status == TASK_DONE) {
                t->status = TASK_DONE;              /* закрита лишається закритою */
            } else if (old[j].time_utc == t->time_utc) {
                t->status      = old[j].status;     /* той самий час: стан зберігаємо */
                ts->since_s[i] = old[j].since_s;
                ts->sent[i]    = old[j].sent;
                ts->notify[i]  = old[j].notify;
            }                                       /* час змінили: SCHEDULED */
            break;
        }
    }
    ts->has_schedule = true;

    if (out)
        make_event(out, EVT_SCHEDULE_UPDATED, now_us, 0,
                   (int32_t)ts->sched.schedule_version, NULL);
    return FD_OK;
}

fd_err_t ts_restore(task_scheduler_t *ts, const schedule_t *saved)
{
    if (!ts || !saved || saved == &ts->sched) return FD_ERR_INVALID_ARG;
    if (!schedule_valid(saved, true)) return FD_ERR_INVALID_ARG;

    ts->sched = *saved;
    terminate_texts(&ts->sched);
    memset(ts->since_s, 0, sizeof(ts->since_s));
    memset(ts->sent, 0, sizeof(ts->sent));
    memset(ts->notify, 0, sizeof(ts->notify));
    for (int i = 0; i < ts->sched.task_count; i++)
        if (ts->sched.tasks[i].status == TASK_DUE) ts->notify[i] = true;
    ts->has_schedule = true;
    return FD_OK;
}

bool ts_handle_event(task_scheduler_t *ts, const event_t *in, event_t *out)
{
    if (!ts || !in || !out) return false;

    int idx;
    task_status_t next;

    if (in->type == EVT_TASK_SEEN) {
        idx = in->id ? find_idx(ts, in->id) : first_with_status(ts, TASK_DUE);
        if (idx < 0 || ts->sched.tasks[idx].status != TASK_DUE) return false;
        next = TASK_SEEN;
    } else if (in->type == EVT_TASK_DONE) {
        if (in->id) {
            idx = find_idx(ts, in->id);
        } else {
            idx = first_with_status(ts, TASK_SEEN);
            if (idx < 0) idx = first_with_status(ts, TASK_DUE);
        }
        if (idx < 0 || ts->sched.tasks[idx].status == TASK_DONE) return false;
        next = TASK_DONE;
    } else {
        return false;
    }

    ts->sched.tasks[idx].status = next;
    ts->since_s[idx] = 0;
    ts->notify[idx]  = false;
    make_event(out, in->type, in->ts_us, ts->sched.tasks[idx].id, 0,
               ts->sched.tasks[idx].text);
    return true;
}

bool ts_tick(task_scheduler_t *ts, uint32_t dt_s, fd_unix_t now_utc,
             fd_time_us_t now_us, event_t *out)
{
    if (!ts || !out) return false;
    const int n = ts->sched.task_count;

    for (int i = 0; i < n; i++)
        if (ts->sched.tasks[i].status == TASK_DUE)
            ts->since_s[i] = sat_add_u32(ts->since_s[i], dt_s);

    /* 1. Задача, час якої настав. */
    if (now_utc >= 0) {
        for (int i = 0; i < n; i++) {
            task_t *t = &ts->sched.tasks[i];
            if (t->status == TASK_SCHEDULED && t->time_utc <= now_utc) {
                t->status  = TASK_DUE;
                ts->sent[i] = 0;
                make_due_event(ts, i, now_us, out);
                return true;
            }
        }
    }

    /* 2. DUE-задача, відновлена після перезапуску. */
    for (int i = 0; i < n; i++) {
        if (ts->sched.tasks[i].status == TASK_DUE && ts->notify[i]) {
            make_due_event(ts, i, now_us, out);
            return true;
        }
    }

    /* 3. Повтор нагадування до «бачу». */
    if (ts->cfg.task_repeat_s > 0) {
        for (int i = 0; i < n; i++) {
            if (ts->sched.tasks[i].status == TASK_DUE &&
                ts->since_s[i] >= ts->cfg.task_repeat_s) {
                make_due_event(ts, i, now_us, out);
                return true;
            }
        }
    }
    return false;
}

const schedule_t *ts_schedule(const task_scheduler_t *ts)
{
    return ts ? &ts->sched : NULL;
}

uint32_t ts_schedule_version(const task_scheduler_t *ts)
{
    return (ts && ts->has_schedule) ? ts->sched.schedule_version : 0;
}

const task_t *ts_find(const task_scheduler_t *ts, uint16_t id)
{
    if (!ts || id == 0) return NULL;
    int i = find_idx(ts, id);
    return i < 0 ? NULL : &ts->sched.tasks[i];
}

const task_t *ts_active(const task_scheduler_t *ts)
{
    if (!ts) return NULL;
    int i = first_with_status(ts, TASK_DUE);
    return i < 0 ? NULL : &ts->sched.tasks[i];
}

uint8_t ts_due_count(const task_scheduler_t *ts)
{
    uint8_t c = 0;
    if (!ts) return 0;
    for (int i = 0; i < ts->sched.task_count; i++)
        if (ts->sched.tasks[i].status == TASK_DUE) c++;
    return c;
}

int32_t ts_local_minute_of_day(const task_t *t)
{
    if (!t) return 0;
    int64_t m = (t->time_utc / 60 + t->tz_offset_min) % 1440;
    if (m < 0) m += 1440;
    return (int32_t)m;
}

const char *ts_status_name(task_status_t s)
{
    switch (s) {
    case TASK_SCHEDULED: return "SCHEDULED";
    case TASK_DUE:       return "DUE";
    case TASK_SEEN:      return "SEEN";
    case TASK_DONE:      return "DONE";
    default:             return "?";
    }
}
