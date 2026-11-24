/*
 * presence_hw — UART-драйвер радара LD2410C (ESP-IDF, B3).
 * Лише залізо: ініціалізація UART і читання байт. Розбір кадрів — у
 * ld2410_parser, фільтрація — у presence_filter.
 */
#ifndef PRESENCE_HW_H
#define PRESENCE_HW_H

#include <stddef.h>
#include <stdint.h>
#include "focusdesk_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PRESENCE_UART_NUM    1
#define PRESENCE_BAUD        256000
#define PRESENCE_PIN_A       18     /* за схемою A: 21 = RX, 18 = TX */
#define PRESENCE_PIN_B       21

/* Запустити UART. rx_pin/tx_pin: номери GPIO ESP32 (rx — куди йде TX радара). */
fd_err_t presence_hw_init(int rx_pin, int tx_pin);

/* Зупинити UART (перед перемиканням пінів). */
void presence_hw_deinit(void);

/* Прочитати до max байт, чекаючи не довше timeout_ms. Повертає кількість. */
int presence_hw_read(uint8_t *buf, size_t max, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
#endif /* PRESENCE_HW_H */
