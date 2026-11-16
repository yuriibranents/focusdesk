# event_bus
Черги q_events (16) і q_ui (8) на FreeRTOS + `fd_heartbeat` для watchdog.
Власник: B.

- `event_bus.h`: init/deinit, post/receive для подій і команд UI, лічильники
  відкинутих повідомлень. Черга повна -> `FD_ERR_QUEUE_FULL`, подія втрачається.
- `fd_heartbeat.h`: чистий C. Задачі викликають `fd_hb_beat`, Monitor Task раз
  на секунду `fd_hb_stale`; якщо хтось мовчить >= 10 с, Monitor перестає
  годувати апаратний watchdog і плата перезавантажується.

Залежності: focusdesk_common, freertos, esp_timer, log.
Тести (на ПК, з фейковим FreeRTOS у test/host/fake):
test/host/test_event_bus.c, test/host/test_fd_heartbeat.c.
