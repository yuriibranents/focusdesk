"""Розбір аргументів команд бота. Чистий Python, без aiogram і БД.

Кожна функція кидає ParseError, повідомлення якої (.args[0]) можна показувати
користувачеві як є.
"""
import re
from datetime import datetime
from zoneinfo import ZoneInfo

MAX_TEXT_LEN = 32          # ліміт тексту на екрані пристрою (ui_cmd_t, розділ 7.3) і CHECK у db.py
MIN_MINUTES = 1
MAX_MINUTES = 600          # CHECK у db.py

# 2h, 90m, 1h30m, 2г, 90хв, 1год 30хв, 45 (просто число = хвилини)
_DUR_RE = re.compile(
    r"^(?:(\d+)\s*(?:год|г|h))?\s*(?:(\d+)\s*(?:min|m|хв)?)?$", re.IGNORECASE
)
_TIME_RE = re.compile(r"^([01]?\d|2[0-3]):([0-5]\d)$")


class ParseError(ValueError):
    pass


def parse_duration(raw: str) -> int:
    m = _DUR_RE.match(raw.strip())
    if not m or not (m.group(1) or m.group(2)):
        raise ParseError(f"Не розумію тривалість «{raw}». Приклади: 2h, 90m, 1h30m, 45")
    minutes = int(m.group(1) or 0) * 60 + int(m.group(2) or 0)
    if not MIN_MINUTES <= minutes <= MAX_MINUTES:
        raise ParseError(f"Тривалість має бути від {MIN_MINUTES} хв до {MAX_MINUTES // 60} год.")
    return minutes


def _check_text(text: str, what: str) -> str:
    text = text.strip()
    if not text:
        raise ParseError(f"{what} не може бути порожньою.")
    if len(text) > MAX_TEXT_LEN:
        raise ParseError(
            f"{what} задовга: {len(text)} символів, максимум {MAX_TEXT_LEN} "
            "(стільки влізе на екран пристрою)."
        )
    return text


def parse_focus_args(args: str | None) -> tuple[str, int]:
    """'Англійська мова 2h' -> ('Англійська мова', 120). Тривалість — останнє слово."""
    parts = (args or "").strip().rsplit(maxsplit=1)
    if len(parts) < 2:
        raise ParseError("Формат: /focus <назва> <тривалість>\nНаприклад: /focus Англійська 2h")
    title = _check_text(parts[0], "Назва цілі")
    return title, parse_duration(parts[1])


def parse_task_args(args: str | None, now: datetime, tz: ZoneInfo) -> tuple[str, str]:
    """'15:30 Піти в магазин' -> ('15:30', 'Піти в магазин').

    Повертає місцевий час 'HH:MM' (саме його чекає Database.add_task, UTC-конвертація там).
    `now` — поточний час (tz-aware). Час, що вже минув, відхиляємо, а не переносимо
    мовчки на завтра: розклад пристрою — це розклад одного дня.
    """
    parts = (args or "").strip().split(maxsplit=1)
    if len(parts) < 2:
        raise ParseError("Формат: /task <HH:MM> <текст>\nНаприклад: /task 15:30 Піти в магазин")
    tm = _TIME_RE.match(parts[0])
    if not tm:
        raise ParseError(f"Не розумію час «{parts[0]}». Потрібен формат HH:MM, наприклад 15:30")
    text = _check_text(parts[1], "Текст задачі")

    hour, minute = int(tm.group(1)), int(tm.group(2))
    local_now = now.astimezone(tz)
    due_local = local_now.replace(hour=hour, minute=minute, second=0, microsecond=0)
    if due_local <= local_now:
        raise ParseError(
            f"Час {hour:02d}:{minute:02d} уже минув (зараз {local_now:%H:%M}). "
            "Вкажіть час пізніше сьогодні."
        )
    return f"{hour:02d}:{minute:02d}", text
