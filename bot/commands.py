"""Логіка команд бота без aiogram: приймає аргументи, повертає текст відповіді.

Так її можна тестувати без Telegram (див. test/host/test_bot_commands.py),
а handlers.py лишається тонкою обгорткою.

db      — bot.db.Database
publish — async-функція publish(schedule: dict) -> None; кидає виняток, якщо
          розклад не вдалося відправити (D4: bot/mqtt_client.py)
now     — поточний час, tz-aware
"""
import logging
from datetime import datetime
from typing import Awaitable, Callable

from .db import Database
from .parsing import ParseError, parse_focus_args, parse_task_args

log = logging.getLogger(__name__)

Publish = Callable[[dict], Awaitable[None]]

HELP = (
    "FocusDesk бот.\n"
    "/focus <назва> <тривалість> — ціль, напр. /focus Англійська 2h\n"
    "/task <HH:MM> <текст> — задача, напр. /task 15:30 Піти в магазин\n"
    "/today — план на сьогодні"
)


async def _sync(db: Database, publish: Publish, day: str) -> str:
    """Публікує розклад дня на пристрій. Повертає рядок для відповіді користувачу."""
    try:
        await publish(db.get_schedule(day))
    except Exception:
        log.exception("Не вдалося опублікувати розклад")
        return "⚠️ Збережено, але розклад ще не надіслано на пристрій (немає зв'язку з брокером)."
    return "📡 Розклад надіслано на пристрій."


async def focus(db: Database, publish: Publish, args: str | None, now: datetime) -> str:
    try:
        title, minutes = parse_focus_args(args)
    except ParseError as e:
        return f"❌ {e.args[0]}"
    day = now.astimezone(db.tz).date().isoformat()
    goal_id = db.add_goal(title, minutes, day)
    sync_line = await _sync(db, publish, day)
    return f"✅ Ціль #{goal_id} збережено: {title}, {minutes // 60} год {minutes % 60} хв.\n{sync_line}"


async def task(db: Database, publish: Publish, args: str | None, now: datetime) -> str:
    try:
        hhmm, text = parse_task_args(args, now, db.tz)
    except ParseError as e:
        return f"❌ {e.args[0]}"
    day = now.astimezone(db.tz).date().isoformat()
    task_id = db.add_task(hhmm, text, day)
    sync_line = await _sync(db, publish, day)
    return f"✅ Задачу #{task_id} збережено: {hhmm} — {text}.\n{sync_line}"


def today(db: Database, now: datetime) -> str:
    local = now.astimezone(db.tz)
    schedule = db.get_schedule(local.date().isoformat())
    if not schedule["goals"] and not schedule["tasks"]:
        return "На сьогодні нічого не заплановано."
    lines = [f"План на {local:%d.%m.%Y}:"]
    if schedule["goals"]:
        lines.append("\nЦілі:")
        lines += [f"  #{g['id']} {g['title']} — {g['minutes']} хв" for g in schedule["goals"]]
    if schedule["tasks"]:
        lines.append("\nЗадачі:")
        for t in schedule["tasks"]:  # get_schedule вже віддає їх за часом, без виконаних
            t_local = datetime.fromisoformat(t["time"].replace("Z", "+00:00")).astimezone(db.tz)
            lines.append(f"  #{t['id']} {t_local:%H:%M} — {t['text']}")
    return "\n".join(lines)
