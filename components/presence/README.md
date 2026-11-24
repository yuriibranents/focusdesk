# presence (B3)

Радар LD2410C -> події PRESENCE_BACK / PRESENCE_LOST.

- ld2410_parser: кадри даних по байту (чистий C, тести на ПК)
- presence_filter: дебаунс BACK 2 с / LOST 10 с (з config), fail-safe: якщо
  кадрів немає 3 с -> один EVT_SYSTEM_ERROR (FD_ERR_SENSOR), LOST не видається,
  дебаунс стартує заново, коли кадри повернуться
- presence_hw: UART1, 256000 бод, RX GPIO21 / TX GPIO18 (за схемою A)
- main/app_tasks.c: якщо 5 с немає жодного кадру, RX/TX міняються місцями

Прапорці збірки: FD_SIM_PRESENCE (0 за замовчуванням: справжній радар),
FD_SIM_DEMO (1: фейковий UTC, демо-задача, кнопки «бачу»/«виконано»).
Лог раз на секунду: `radar: frames=... raw=... stable=PRESENT/ABSENT`.
