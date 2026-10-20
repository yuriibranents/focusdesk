# FocusDesk

Розумний настільний пристрій на ESP32-S3: контроль сидіння, фокус-сесії, задачі-нагадування з розкладом із Telegram-бота. Працює офлайн.

## Збірка прошивки
```
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

## Host-тести
```
cmake -S test/host -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

## Бот
```
python -m venv .venv
.venv\Scripts\activate
pip install -r bot/requirements.txt
copy .env.example .env
```
Секрети (`.env`, токени, Wi-Fi) не комітяться.

## Git-процес
- `main` завжди збирається; гілки `feature/<роль>-<тема>`; злиття лише через Pull Request із рев'ю напарника.
- Збірка щоп'ятниці: тег `week-N`. Блокери: GitHub Issues.

Версії: docs/versions.md. Тест-план: docs/test_plan.md.