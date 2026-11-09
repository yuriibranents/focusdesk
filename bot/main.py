"""Точка входу бота: python -m bot.main (запускати з кореня репозиторію)."""
import asyncio
import logging
import os

from aiogram import Bot, Dispatcher
from dotenv import load_dotenv

from .db import Database
from .handlers import build_router

log = logging.getLogger(__name__)


def _load_publisher():
    """Бере publish_schedule з bot/mqtt_client.py (D4), а поки його немає — заглушка.

    Заглушка кидає виняток, тож бот чесно відповідає «збережено, але не надіслано»,
    а не вдає, що розклад дійшов.
    """
    try:
        from .mqtt_client import publish_schedule
        return publish_schedule
    except ModuleNotFoundError as e:
        if e.name != f"{__package__}.mqtt_client":
            raise  # це не «модуля ще немає», а справжня помилка імпорту всередині нього

        async def stub(schedule: dict) -> None:
            raise RuntimeError("MQTT-клієнт ще не підключено (завдання D4)")

        return stub


def _require_env(name: str) -> str:
    value = os.environ.get(name, "").strip()
    if not value:
        raise SystemExit(f"Не задано {name}. Скопіюйте .env.example у .env і заповніть.")
    return value


async def main() -> None:
    load_dotenv()
    token = _require_env("BOT_TOKEN")
    try:
        chat_id = int(_require_env("ALLOWED_CHAT_ID"))
    except ValueError:
        raise SystemExit("ALLOWED_CHAT_ID має бути числом (id вашого чату з ботом).")

    db = Database(os.environ.get("DB_PATH", "focusdesk.db"))
    dp = Dispatcher()
    dp.include_router(build_router(db, _load_publisher(), chat_id))
    bot = Bot(token=token)
    log.info("Бот запущено")
    await dp.start_polling(bot)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    asyncio.run(main())
