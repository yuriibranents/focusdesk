# Розгортання Mosquitto (MQTT-брокера) для FocusDesk

> Завдання **D6**. Власник: D. Напарник для рев'ю: C.
> Версія 1.0 · потрібна для: C7 (MQTT-клієнт пристрою), D4 (клієнт бота), E5, E10.

Значення параметрів (keepalive, QoS, таймаути) — стартові, як у розділі 7.5 опису проєкту; уточнюються за результатами E5 і E10.

## 1. Що розгортаємо

```
Telegram-бот (Python, paho-mqtt) ──┐
                                   ├──► Mosquitto :1883 ◄── ESP32-S3 (esp-mqtt)
device_emulator.py ────────────────┘
```

Брокер працює на ПК/міні-сервері у тій самій локальній мережі, що й Wi-Fi пристрою. Для демо це найпростіше й найнадійніше (не залежить від інтернету — це ще й частина сценарію E5).

Теми (розділ 8.1 опису):

| Тема | Напрям | Прапорці |
|---|---|---|
| `focusdesk/<id>/schedule` | бот → пристрій | **retained**, QoS 1 |
| `focusdesk/<id>/event` | пристрій → бот | QoS 1 |
| `focusdesk/<id>/status` | пристрій → бот | LWT `offline`, QoS 1 |
| `focusdesk/<id>/ack` | пристрій → бот | QoS 1 |

`<id>` — MAC пристрою без двокрапок (для емулятора — довільний, напр. `emu01`).

## 2. Встановлення

### Варіант A. Ubuntu/Debian (рекомендований)

```bash
sudo apt update
sudo apt install -y mosquitto mosquitto-clients
mosquitto -h | head -1          # зафіксуйте версію в docs/versions.md (очікується 2.x)
```

### Варіант B. Docker

`deploy/docker-compose.yml`:

```yaml
services:
  mosquitto:
    image: eclipse-mosquitto:2.0.18   # зафіксована версія, без :latest
    restart: unless-stopped
    ports:
      - "1883:1883"
    volumes:
      - ./mosquitto/config:/mosquitto/config
      - ./mosquitto/data:/mosquitto/data
      - ./mosquitto/log:/mosquitto/log
```

Запуск: `docker compose up -d`.

### Варіант C. Windows

Інсталятор з mosquitto.org; конфіг — `C:\Program Files\mosquitto\mosquitto.conf`; служба `Mosquitto Broker` у `services.msc`. Решта кроків така сама (клієнти `mosquitto_pub/sub` лежать у папці встановлення).

## 3. Конфігурація

`/etc/mosquitto/conf.d/focusdesk.conf` (для Docker — `mosquitto/config/mosquitto.conf`, додайте рядок `persistence_location /mosquitto/data/` і шляхи `/mosquitto/config/...`):

```conf
# --- збереження retained-розкладу після перезапуску брокера ---
persistence true
persistence_location /var/lib/mosquitto/
autosave_interval 60

# --- слухач ---
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
acl_file /etc/mosquitto/acl

# --- логи ---
log_dest file /var/log/mosquitto/mosquitto.log
log_type error
log_type warning
log_type notice
log_type information
connection_messages true
```

**Чому `persistence true` важливо.** Без нього retained-розклад зникає при перезапуску брокера, і пристрій після реконекту не отримає план дня до наступної публікації від бота.

### 3.1 Користувачі

```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd bot       # -c лише для першого користувача
sudo mosquitto_passwd    /etc/mosquitto/passwd device
sudo chown mosquitto: /etc/mosquitto/passwd && sudo chmod 600 /etc/mosquitto/passwd
```

Паролі — у `.env` бота та в NVS пристрою (через `sdkconfig.local`). **У Git не комітимо** жодних паролів і файлу `passwd`.

### 3.2 ACL (`/etc/mosquitto/acl`)

```conf
user bot
topic write focusdesk/+/schedule
topic read  focusdesk/+/event
topic read  focusdesk/+/status
topic read  focusdesk/+/ack

user device
topic read  focusdesk/+/schedule
topic write focusdesk/+/event
topic write focusdesk/+/status
topic write focusdesk/+/ack
```

Це мінімальний варіант (один спільний акаунт `device` для всіх пристроїв і емуляторів). Якщо буде час — окремий акаунт на пристрій і `pattern`-правила.

### 3.3 Запуск і автозапуск

```bash
sudo systemctl restart mosquitto
sudo systemctl enable mosquitto
sudo systemctl status mosquitto --no-pager
sudo journalctl -u mosquitto -n 30 --no-pager      # помилок конфігу не має бути
```

### 3.4 Мережа

- Брокеру потрібна статична IP-адреса або DHCP-резервація (адресу пристрій отримує через `sync`/NVS).
- Відкрити порт 1883 лише для локальної мережі:
  ```bash
  sudo ufw allow from 192.168.0.0/16 to any port 1883 proto tcp
  ```
- Не виставляти 1883 в інтернет. TLS (8883) — опційно, після GO/NO-GO.
- Дізнатися адресу: `ip -4 addr` (Linux), `ipconfig` (Windows). Передати C для `docs/protocol.md`.

## 4. Smoke-тест (5 хвилин)

Три термінали. Змінні для зручності:

```bash
export H=localhost ID=emu01
```

**Термінал 1 — підписка на все:**
```bash
mosquitto_sub -h $H -u bot -P '<пароль bot>' -t "focusdesk/#" -v
```

**Термінал 2 — публікація розкладу (як бот):**
```bash
mosquitto_pub -h $H -u bot -P '<пароль bot>' -q 1 -r -t "focusdesk/$ID/schedule" -m '{
  "schedule_version": 1,
  "date": "2026-10-20",
  "goals": [{"id": 1, "title": "Англійська", "minutes": 120}],
  "tasks": [{"id": 11, "time": "2026-10-20T12:30:00Z", "text": "Піти в магазин"}]
}'
```

**Перевірка retained:** новий підписник одразу отримує розклад.
```bash
mosquitto_sub -h $H -u device -P '<пароль device>' -t "focusdesk/$ID/schedule" -C 1 -W 5 -v
```
Очікування: повідомлення приходить негайно, без нової публікації.

**Перевірка ACL:** `device` не може писати в `schedule`, `bot` не може писати в `event`:
```bash
mosquitto_pub -h $H -u device -P '<пароль device>' -t "focusdesk/$ID/schedule" -m x
# Mosquitto мовчки відкидає такі повідомлення — перевіряйте, що підписник у терміналі 1 їх НЕ побачив
```

**Подія від пристрою:**
```bash
mosquitto_pub -h $H -u device -P '<пароль device>' -q 1 -t "focusdesk/$ID/event" \
  -m '{"ts":"2026-10-20T14:05:11Z","type":"FOCUS_DONE","goal_id":1,"title":"Англійська","minutes":118,"paused_s":240}'
```

**Скидання retained-повідомлення (за потреби):**
```bash
mosquitto_pub -h $H -u bot -P '<пароль bot>' -r -n -t "focusdesk/$ID/schedule"
```

## 5. Перевірка при розриві зв'язку

Мета: переконатися, що розрив не ламає ні пристрій, ні бота, і що після відновлення дані доходять. Ця секція — основа для E5 і E10.

Сценарії запускаємо спочатку з `tools/device_emulator.py`, потім повторюємо з реальною платою. Нагадування: стартові значення — keepalive пристрою **30 с**, QoS **1**; брокер оголошує клієнта відключеним приблизно через **1,5 × keepalive ≈ 45 с**.

Вимоги до клієнтів (узгодити з C для пристрою; для бота — моя зона):

- клієнт має автореконект (esp-mqtt — увімкнено за замовчуванням; paho — `reconnect_delay_set` або власний цикл);
- підписки відновлюються в `on_connect`, а не один раз на старті;
- `clean_session = false` (persistent session) для пристрою — щоб QoS 1 повідомлення, надіслані під час відсутності, були доставлені;
- LWT на `focusdesk/<id>/status` = `offline`, а після підключення публікується `online` (рекомендовано обидва — retained, щоб бот бачив стан одразу; домовитись із C).

### Сценарії

| № | Дія | Очікуваний результат | Що міряємо |
|---|---|---|---|
| **N1** | Зупинити брокер: `sudo systemctl stop mosquitto`, почекати 2 хв, запустити | Пристрій/емулятор не перезавантажуються; нагадування та фокус-сесія працюють з локальної копії. Після старту брокера клієнти самі підключаються | Час реконекту (с), кількість втрачених нагадувань = 0 |
| **N2** | Під час N1 надіслати боту `/task` | Бот не падає, повідомляє користувачу, що розклад збережено, але ще не доставлено (або ставить публікацію в повторну спробу) | Реакція бота; відсутність необроблених винятків у логу |
| **N3** | Після N1/N2 перевірити розклад на пристрої | Пристрій отримує останній retained-розклад; `schedule_version` збільшилась; `ack` прийшов у бот | Затримка «брокер піднявся → ack» (с) |
| **N4** | Перезапустити брокер, не очищуючи дані: `sudo systemctl restart mosquitto`; нова підписка на `schedule` | Retained-розклад на місці (працює `persistence`) | Є / немає |
| **N5** | Тихий розрив (без FIN): на сервері `sudo iptables -I INPUT -p tcp --dport 1883 -j DROP`, чекати 60 с, потім `sudo iptables -D INPUT -p tcp --dport 1883 -j DROP` | Через ~45 с у логу брокера відключення клієнта по таймауту й публікація LWT `offline`; після зняття правила клієнт повертається, `online` | Час до LWT (с), час до повернення (с) |
| **N6** | Вимкнути Wi-Fi на роутері / відключити точку доступу на 5 хв, увімкнути | Пристрій переходить у «офлайн» (за потреби індикація на екрані), нагадування працюють (E5), після Wi-Fi — SNTP + MQTT відновлюються | Нагадування за час розриву; час до `online` |
| **N7** | Перезапустити бота (`Ctrl+C`, запуск) під час роботи пристрою | Бот перепідписується, читає `status`/`event`; пристрій нічого не помічає | Втрачені події (мають бути 0 при QoS 1) |
| **N8** | Публікація розкладу, поки пристрій офлайн (N6), потім його повернення | Пристрій отримує retained-розклад на ту ж тему, застосовує за `schedule_version` | Доставлено? Затримка від реконекту |

На Windows замість `iptables` — вимкнути мережевий адаптер або правило брандмауера на порт 1883.

### Журнал результатів (заповнюється в E5/E10)

Скопіюйте таблицю в `docs/test_plan.md`, додайте дату, версію прошивки/бота, хто виконував.

| № | Дата | Пристрій (плата / емулятор) | Результат (пройдено / ні) | Виміри | Дефект (Issue) |
|---|---|---|---|---|---|
| N1 | | | | | |
| N2 | | | | | |
| N3 | | | | | |
| N4 | | | | | |
| N5 | | | | | |
| N6 | | | | | |
| N7 | | | | | |
| N8 | | | | | |

Для E10 (синхронізація) повторюйте вимір «команда боту → розклад на пристрої» не менше 30 разів і рахуйте медіану та p95.

## 6. Діагностика

| Симптом | Перевірити |
|---|---|
| Клієнт: `Connection refused: not authorised` | Пароль у `.env`/NVS; `mosquitto_passwd`; права на файл `passwd` |
| Клієнт підключається, але повідомлення «зникають» | ACL: ім'я користувача й тема; логи брокера |
| Пристрій не бачить брокера | IP брокера, фаєрвол (`ufw status`), одна підмережа, `listener 1883` без `bind_address`, що обмежує до localhost |
| Retained-розклад зникає після рестарту | `persistence true`, права на `/var/lib/mosquitto/`, дисковий простір |
| LWT приходить із запізненням | Це нормально: ~1,5 × keepalive; зменшіть keepalive, якщо потрібна швидша реакція (але більше трафіку) |
| Помилки в `journalctl` після зміни конфігу | `mosquitto -c <файл> -v` у foreground для діагностики |
| Дублікати повідомлень | QoS 1 дає «щонайменше раз» — клієнти мають бути ідемпотентними (перевірка за `schedule_version`/`id`) |

Корисні команди: `sudo tail -f /var/log/mosquitto/mosquitto.log`, `mosquitto_sub -t '$SYS/#' -v` (статистика брокера; для користувача `bot` потрібне окреме ACL-правило `topic read $SYS/#`).

## 7. Що не комітимо в Git

У `.gitignore`: `passwd`, `.env`, `sdkconfig.local`, `mosquitto/data/`, `mosquitto/log/`. У репозиторії лишаємо `mosquitto.conf.example`, `acl.example`, `docker-compose.yml` і цей документ.

## 8. Definition of Done для D6

- [ ] Mosquitto запускається за цією інструкцією з нуля (перевірено напарником C на чистій машині)
- [ ] Версія брокера записана в `docs/versions.md`
- [ ] Автозапуск увімкнено; брокер переживає перезавантаження ПК
- [ ] Smoke-тест (розд. 4) пройдено; retained і ACL працюють
- [ ] Сценарії N1–N8 виконано з емулятором, результати в `docs/test_plan.md`; N1, N5, N6, N8 повторено з реальною платою після готовності C7
- [ ] Бот перепідключається після розриву й перезапуску брокера без ручних дій
- [ ] Адреса брокера й облікові дані передані C (поза Git) і прописані в `docs/protocol.md`
- [ ] Заведено Issues на всі знайдені дефекти; PR пройшов CI та рев'ю напарника
