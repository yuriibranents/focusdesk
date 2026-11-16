/*
 * app_tasks — п'ять задач FreeRTOS і watchdog (B2, розділ 7.2).
 *
 *   Presence Task (ядро 1, пріоритет 5)  заглушка до B3 (драйвер LD2410C)
 *   Logic Task    (ядро 1, пріоритет 4)  автомати sitting_monitor і focus_session
 *   UI Task       (ядро 1, пріоритет 3)  заглушка до роботи C (екран, кільце, звук)
 *   Sync Task     (ядро 0, пріоритет 3)  заглушка до роботи D (Wi-Fi, MQTT)
 *   Monitor Task  (без ядра, пріоритет 6) перевіряє heartbeat, при зависанні
 *                                         перезавантажує пристрій
 *
 * Контракт heartbeat: кожна задача (і її майбутня справжня реалізація) мусить
 * викликати app_heartbeat(FD_HB_xxx) хоча б раз на ~секунду. Якщо задача
 * мовчить FD_HB_TIMEOUT_MS (10 с), Monitor пише помилку й шле EVT_SYSTEM_ERROR;
 * якщо мовчить ще FD_WD_GRACE_MS (5 с), робить esp_restart().
 *
 * FD_SIM_PRESENCE=1 (за замовчуванням, поки немає B3): заглушка присутності
 * імітує людину (з'явилась на 3-й с, відійшла на 23-й, цикл 40 с).
 * Це лише для перевірки ланцюжка подій. Прибирається в PR з драйвером B3.
 */
#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "focusdesk_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Створює черги й усі задачі. Викликається один раз з app_main. */
fd_err_t app_tasks_start(void);

/* Відзвітуватися, що задача жива: bits = FD_HB_PRESENCE/LOGIC/UI/SYNC. */
void app_heartbeat(uint32_t bits);

#ifdef __cplusplus
}
#endif
#endif /* APP_TASKS_H */
