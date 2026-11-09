# sitting_monitor
Автомат контролю безперервного сидіння (ABSENT / SITTING / AWAY / BREAK_DUE).
Чистий C, без ESP-IDF. Власник: B.

API: `sm_init`, `sm_handle_event`, `sm_tick`, `sm_state`, `sm_sit_seconds`.
Logic Task викликає `sm_tick(&sm, 1, now_us, &out)` раз на секунду і
`sm_handle_event` на кожну PRESENCE_* подію; вихідні події (BREAK_DUE,
BREAK_DONE) кладе в чергу подій.
Залежності: focusdesk_common. Тести: test/host/test_sitting_monitor.c.
