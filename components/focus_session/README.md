# focus_session
Автомат фокус-сесії (IDLE / READY / RUNNING / PAUSED / SUSPENDED / DONE).
Чистий C, без ESP-IDF. Власник: B.

API: `fs_init`, `fs_select_goal`, `fs_cancel`, `fs_handle_event`, `fs_tick`,
`fs_state`, `fs_remaining_seconds`, `fs_paused_seconds`, `fs_goal`.
Logic Task викликає `fs_tick(&fs, 1, now_us, &out)` раз на секунду і
`fs_handle_event` на кожну подію PRESENCE_* та EVT_FOCUS_START (кнопка);
вихідні події (START, PAUSE, RESUME, SUSPENDED, DONE) кладе в чергу подій.
Вибір цілі енкодером: `fs_select_goal`. Опис переходів: див. focus_session.h.
Параметр з config_t: suspend_after_min (10).
Залежності: focusdesk_common. Тести: test/host/test_focus_session.c.
