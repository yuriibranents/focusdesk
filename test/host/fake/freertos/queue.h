/* Імітація черги: кільцевий буфер, без блокування (timeout ігнорується). */
#ifndef FAKE_FREERTOS_QUEUE_H
#define FAKE_FREERTOS_QUEUE_H
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"

typedef struct {
    unsigned char *buf;
    unsigned len, item, head, count;
} fake_queue_t;
typedef fake_queue_t *QueueHandle_t;

static inline QueueHandle_t xQueueCreate(unsigned len, unsigned item)
{
    fake_queue_t *q = (fake_queue_t *)calloc(1, sizeof(*q));
    if (!q) return NULL;
    q->buf = (unsigned char *)calloc(len, item);
    if (!q->buf) { free(q); return NULL; }
    q->len = len; q->item = item;
    return q;
}
static inline void vQueueDelete(QueueHandle_t q)
{
    if (q) { free(q->buf); free(q); }
}
static inline BaseType_t xQueueSend(QueueHandle_t q, const void *it, TickType_t t)
{
    (void)t;
    if (q->count == q->len) return pdFALSE;
    memcpy(q->buf + ((q->head + q->count) % q->len) * q->item, it, q->item);
    q->count++;
    return pdTRUE;
}
static inline BaseType_t xQueueReceive(QueueHandle_t q, void *it, TickType_t t)
{
    (void)t;
    if (q->count == 0) return pdFALSE;
    memcpy(it, q->buf + q->head * q->item, q->item);
    q->head = (q->head + 1) % q->len;
    q->count--;
    return pdTRUE;
}
#endif
