#include "arbiter.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Допоміжні функції                                                   */
/* ------------------------------------------------------------------ */

/* Копіює рядок не довше FD_TEXT_MAX - 1 байт; джерело може бути коротшим
 * за FD_TEXT_MAX (літерал) або без '\0' у межах буфера. Результат завжди
 * завершений нулем. */
static void copy_text(char *dst, const char *src)
{
    memset(dst, 0, FD_TEXT_MAX);
    if (!src) return;
    for (size_t i = 0; i < FD_TEXT_MAX - 1 && src[i] != '\0'; i++)
        dst[i] = src[i];
}

static uint32_t sub_sat(uint32_t a, uint32_t b)
{
    return a > b ? a - b : 0;
}

static int find_due(const arbiter_t *a, uint16_t id)
{
    for (int i = 0; i < a->due_count; i++)
        if (a->due[i].id == id) return i;
    return -1;
}

static void remove_due(arbiter_t *a, int idx)
{
    for (int i = idx; i < a->due_count - 1; i++) a->due[i] = a->due[i + 1];
    a->due_count--;
    memset(&a->due[a->due_count], 0, sizeof(a->due[0]));
    if (a->due_count == 0) a->snd_task = false;
}

static uint32_t make_ident(arb_layer_t layer, uint8_t sub, uint16_t key)
{
    return ((uint32_t)layer << 24) | ((uint32_t)sub << 16) | key;
}

static uint8_t progress_pct(const arbiter_t *a)
{
    if (a->focus_total_s == 0) return 0;
    uint32_t done = sub_sat(a->focus_total_s, a->focus_remaining_s);
    uint64_t pct = (uint64_t)done * 100u / a->focus_total_s;
    return pct > 100u ? 100u : (uint8_t)pct;
}

static void set_rgb(ui_cmd_t *v, uint8_t r, uint8_t g, uint8_t b)
{
    v->r = r; v->g = g; v->b = b;
}

/* Збирає вигляд, що відповідає найвищому активному шару. sound = NONE. */
static arb_layer_t build_view(const arbiter_t *a, ui_cmd_t *v, uint32_t *ident)
{
    memset(v, 0, sizeof(*v));
    v->sound = SOUND_NONE;

    if (a->due_count > 0) {
        v->screen = SCREEN_TASK;
        v->ring_mode = RING_BLINK_FAST;
        set_rgb(v, ARB_RGB_TASK);
        v->value = a->due_count;
        copy_text(v->text, a->due[0].text);
        *ident = make_ident(ARB_LAYER_TASK, 0, a->due[0].id);
        return ARB_LAYER_TASK;
    }
    if (a->absence_alert) {
        v->screen = SCREEN_ABSENCE_ALERT;
        v->ring_mode = RING_BLINK_SLOW;
        set_rgb(v, ARB_RGB_ABSENCE);
        copy_text(v->text, ARB_TEXT_ABSENCE);
        *ident = make_ident(ARB_LAYER_ABSENCE, 0, 0);
        return ARB_LAYER_ABSENCE;
    }
    if (a->break_due) {
        v->screen = SCREEN_BREAK;
        v->ring_mode = RING_PULSE;
        set_rgb(v, ARB_RGB_BREAK);
        copy_text(v->text, ARB_TEXT_BREAK);
        *ident = make_ident(ARB_LAYER_BREAK, 0, 0);
        return ARB_LAYER_BREAK;
    }
    if (a->focus != ARB_FOCUS_NONE) {
        v->value = (int32_t)a->focus_remaining_s;
        copy_text(v->text, a->focus_title);
        switch (a->focus) {
        case ARB_FOCUS_RUNNING:
            v->screen = SCREEN_FOCUS;
            v->ring_mode = RING_PROGRESS;
            v->ring_value = progress_pct(a);
            set_rgb(v, ARB_RGB_FOCUS);
            break;
        case ARB_FOCUS_PAUSED:
            v->screen = SCREEN_FOCUS_PAUSED;
            v->ring_mode = RING_PULSE;
            set_rgb(v, ARB_RGB_PAUSED);
            break;
        default: /* ARB_FOCUS_SUSPENDED */
            v->screen = SCREEN_FOCUS_PAUSED;
            v->ring_mode = RING_STATIC;
            set_rgb(v, ARB_RGB_SUSPENDED);
            break;
        }
        *ident = make_ident(ARB_LAYER_FOCUS, (uint8_t)a->focus, a->focus_id);
        return ARB_LAYER_FOCUS;
    }
    if (a->done_left_s > 0) {
        v->screen = SCREEN_FOCUS;
        v->ring_mode = RING_PULSE;
        v->ring_value = 100;
        v->value = 0;
        set_rgb(v, ARB_RGB_DONE);
        copy_text(v->text, a->done_title);
        *ident = make_ident(ARB_LAYER_DONE, 0, a->done_id);
        return ARB_LAYER_DONE;
    }
    v->screen = SCREEN_IDLE;
    v->ring_mode = RING_STATIC;
    set_rgb(v, ARB_RGB_IDLE);
    *ident = make_ident(ARB_LAYER_IDLE, 0, 0);
    return ARB_LAYER_IDLE;
}

/* Видає команду, якщо вигляд змінився (інший шар або інший вміст: залишок
 * часу, текст, кількість задач) або force (повтор TASK_DUE, щоб знову дати
 * звук). Звук шару, який вперше став видимим, додається тут. */
static bool emit(arbiter_t *a, bool force, ui_cmd_t *out)
{
    ui_cmd_t v;
    uint32_t ident;
    arb_layer_t layer = build_view(a, &v, &ident);

    bool ident_changed = !a->has_last || ident != a->last_ident;
    if (!ident_changed && !force && memcmp(&v, &a->last, sizeof(v)) == 0)
        return false;           /* на екрані нічого не змінилось */

    sound_t snd = SOUND_NONE;
    if (ident_changed || force) {
        switch (layer) {
        case ARB_LAYER_TASK:
            if (a->snd_task)    { snd = SOUND_BEEP_FAST;  a->snd_task = false; }
            break;
        case ARB_LAYER_ABSENCE:
            if (a->snd_absence) { snd = SOUND_BEEP_SLOW;  a->snd_absence = false; }
            break;
        case ARB_LAYER_BREAK:
            if (a->snd_break)   { snd = SOUND_BEEP_SHORT; a->snd_break = false; }
            break;
        case ARB_LAYER_DONE:
            if (a->snd_done)    { snd = SOUND_BEEP_DONE;  a->snd_done = false; }
            break;
        default:
            break;
        }
    }

    memcpy(&a->last, &v, sizeof(v));
    a->last_ident = ident;
    a->has_last = true;
    if (out) {
        memcpy(out, &v, sizeof(v));
        out->sound = snd;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Публічний API                                                       */
/* ------------------------------------------------------------------ */

void arb_init(arbiter_t *a)
{
    if (!a) return;
    memset(a, 0, sizeof(*a));
    /* Початковий вигляд IDLE вважаємо виданим: UI бере його через arb_current. */
    a->has_last = true;
    a->last_ident = 0;
    build_view(a, &a->last, &a->last_ident);
}

bool arb_handle_event(arbiter_t *a, const event_t *in, ui_cmd_t *out)
{
    if (!a || !in || !out) return false;
    bool force = false;

    switch (in->type) {
    case EVT_TASK_DUE: {
        if (in->id == 0) return false;
        int i = find_due(a, in->id);
        if (i < 0) {
            if (a->due_count >= FD_MAX_TASKS) return false;
            i = a->due_count++;
            a->due[i].id = in->id;
        }
        copy_text(a->due[i].text, in->text);
        a->snd_task = true;
        force = true;                       /* повтор нагадування: звук знову */
        break;
    }
    case EVT_TASK_SEEN:
    case EVT_TASK_DONE: {
        int i = find_due(a, in->id);
        if (i < 0) return false;
        remove_due(a, i);
        break;
    }
    case EVT_ABSENCE_ALERT:
        if (!a->absence_alert) { a->absence_alert = true; a->snd_absence = true; }
        break;
    case EVT_PRESENCE_BACK:
        a->absence_alert = false;
        a->snd_absence = false;
        break;
    case EVT_BREAK_DUE:
        if (!a->break_due) { a->break_due = true; a->snd_break = true; }
        break;
    case EVT_BREAK_DONE:
        a->break_due = false;
        a->snd_break = false;
        break;
    case EVT_FOCUS_START: {
        int32_t min = in->value > 0 ? in->value : 0;
        a->focus = ARB_FOCUS_RUNNING;
        a->focus_id = in->id;
        copy_text(a->focus_title, in->text);
        a->focus_total_s = (uint32_t)min * 60u;
        a->focus_remaining_s = a->focus_total_s;
        a->done_left_s = 0;                 /* новий старт знімає банер */
        a->snd_done = false;
        break;
    }
    case EVT_FOCUS_PAUSE:
    case EVT_FOCUS_RESUME:
    case EVT_FOCUS_SUSPENDED:
        if (a->focus == ARB_FOCUS_NONE) return false;
        a->focus_remaining_s = in->value > 0 ? (uint32_t)in->value : 0;
        a->focus = (in->type == EVT_FOCUS_RESUME)  ? ARB_FOCUS_RUNNING
                 : (in->type == EVT_FOCUS_PAUSE)   ? ARB_FOCUS_PAUSED
                                                   : ARB_FOCUS_SUSPENDED;
        break;
    case EVT_FOCUS_DONE:
        a->done_id = in->id ? in->id : a->focus_id;
        copy_text(a->done_title, in->text[0] ? in->text : a->focus_title);
        a->focus = ARB_FOCUS_NONE;
        a->focus_remaining_s = 0;
        a->done_left_s = ARB_DONE_BANNER_S;
        a->snd_done = true;
        break;
    default:
        return false;
    }
    return emit(a, force, out);
}

bool arb_tick(arbiter_t *a, uint32_t dt_s, ui_cmd_t *out)
{
    if (!a || !out) return false;
    if (a->focus == ARB_FOCUS_RUNNING)
        a->focus_remaining_s = sub_sat(a->focus_remaining_s, dt_s);
    if (a->done_left_s > 0) {
        a->done_left_s = sub_sat(a->done_left_s, dt_s);
        if (a->done_left_s == 0) a->snd_done = false;
    }
    return emit(a, false, out);
}

bool arb_sync_tasks(arbiter_t *a, const task_t *tasks, uint8_t count,
                    ui_cmd_t *out)
{
    if (!a || !out || (!tasks && count > 0)) return false;
    if (count > FD_MAX_TASKS) count = FD_MAX_TASKS;

    /* 1. Прибрати задачі, які більше не DUE (у новому розкладі). */
    for (int i = 0; i < a->due_count; ) {
        bool still = false;
        for (int j = 0; j < count; j++) {
            if (tasks[j].id == a->due[i].id && tasks[j].status == TASK_DUE) {
                copy_text(a->due[i].text, tasks[j].text);
                still = true;
                break;
            }
        }
        if (still) i++; else remove_due(a, i);
    }
    /* 2. Додати DUE-задачі, яких ще немає (без звуку). */
    for (int j = 0; j < count; j++) {
        if (tasks[j].status != TASK_DUE || tasks[j].id == 0) continue;
        if (find_due(a, tasks[j].id) >= 0) continue;
        if (a->due_count >= FD_MAX_TASKS) break;
        a->due[a->due_count].id = tasks[j].id;
        copy_text(a->due[a->due_count].text, tasks[j].text);
        a->due_count++;
    }
    return emit(a, false, out);
}

bool arb_current(const arbiter_t *a, ui_cmd_t *out)
{
    if (!a || !out) return false;
    uint32_t ident;
    build_view(a, out, &ident);
    return true;
}

arb_layer_t arb_layer(const arbiter_t *a)
{
    ui_cmd_t v;
    uint32_t ident;
    if (!a) return ARB_LAYER_IDLE;
    return build_view(a, &v, &ident);
}

const char *arb_layer_name(arb_layer_t l)
{
    switch (l) {
    case ARB_LAYER_IDLE:    return "IDLE";
    case ARB_LAYER_DONE:    return "DONE";
    case ARB_LAYER_FOCUS:   return "FOCUS";
    case ARB_LAYER_BREAK:   return "BREAK";
    case ARB_LAYER_ABSENCE: return "ABSENCE";
    case ARB_LAYER_TASK:    return "TASK";
    default:                return "?";
    }
}
