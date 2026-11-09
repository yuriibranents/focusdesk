"""FocusDesk: шар доступу до SQLite (завдання D2).

Зберігає цілі, задачі та підсумки фокус-сесій. Формат розкладу відповідає
розділу 8.2 технічного опису. Час у БД і в розкладі — UTC (ISO 8601, 'Z');
користувач вводить місцевий час, конвертація — тут (TZ за замовчуванням Europe/Kyiv).

Використовується stdlib sqlite3: виклики короткі, тож не блокують aiogram помітно.
Для суворої асинхронності можна обгорнути в asyncio.to_thread().
"""
from __future__ import annotations

import sqlite3
from contextlib import contextmanager
from datetime import date, datetime, time, timezone
from typing import Iterator, Optional
from zoneinfo import ZoneInfo

DEFAULT_TZ = "Europe/Kyiv"
TASK_STATUSES = ("SCHEDULED", "DUE", "SEEN", "DONE")

SCHEMA = """
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS goals (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    day        TEXT    NOT NULL,                      -- 'YYYY-MM-DD' (місцева дата)
    title      TEXT    NOT NULL CHECK (length(title) BETWEEN 1 AND 32),
    minutes    INTEGER NOT NULL CHECK (minutes BETWEEN 1 AND 600),
    created_at TEXT    NOT NULL,                      -- UTC ISO
    deleted    INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_goals_day ON goals(day, deleted);

CREATE TABLE IF NOT EXISTS tasks (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    day        TEXT    NOT NULL,
    time_utc   TEXT    NOT NULL,                      -- 'YYYY-MM-DDTHH:MM:SSZ'
    text       TEXT    NOT NULL CHECK (length(text) BETWEEN 1 AND 32),
    status     TEXT    NOT NULL DEFAULT 'SCHEDULED'
               CHECK (status IN ('SCHEDULED','DUE','SEEN','DONE')),
    created_at TEXT    NOT NULL,
    done_at    TEXT,
    deleted    INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_tasks_day ON tasks(day, deleted);

CREATE TABLE IF NOT EXISTS session_summaries (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT,
    goal_id   INTEGER REFERENCES goals(id),
    title     TEXT    NOT NULL,
    minutes   INTEGER NOT NULL,                       -- фактична тривалість
    paused_s  INTEGER NOT NULL DEFAULT 0,
    ts        TEXT    NOT NULL                        -- UTC ISO, час події
);
CREATE INDEX IF NOT EXISTS idx_sessions_ts ON session_summaries(ts);
"""


def _utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


class Database:
    def __init__(self, path: str = "focusdesk.db", tz: str = DEFAULT_TZ):
        self.path = path
        self.tz = ZoneInfo(tz)
        self.init_db()

    # --- інфраструктура -------------------------------------------------
    @contextmanager
    def _conn(self) -> Iterator[sqlite3.Connection]:
        conn = sqlite3.connect(self.path)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA foreign_keys = ON")
        try:
            yield conn
            conn.commit()
        except Exception:
            conn.rollback()
            raise
        finally:
            conn.close()

    def init_db(self) -> None:
        with self._conn() as c:
            c.executescript(SCHEMA)
            c.execute("INSERT OR IGNORE INTO meta(key, value) VALUES ('schedule_version', '0')")

    def today(self) -> str:
        return datetime.now(self.tz).date().isoformat()

    def _bump_version(self, c: sqlite3.Connection) -> int:
        c.execute("UPDATE meta SET value = CAST(value AS INTEGER) + 1 WHERE key = 'schedule_version'")
        return int(c.execute("SELECT value FROM meta WHERE key = 'schedule_version'").fetchone()[0])

    def schedule_version(self) -> int:
        with self._conn() as c:
            return int(c.execute("SELECT value FROM meta WHERE key = 'schedule_version'").fetchone()[0])

    # --- цілі -----------------------------------------------------------
    def add_goal(self, title: str, minutes: int, day: Optional[str] = None) -> int:
        day = day or self.today()
        with self._conn() as c:
            cur = c.execute(
                "INSERT INTO goals(day, title, minutes, created_at) VALUES (?,?,?,?)",
                (day, title.strip(), minutes, _utc_now()),
            )
            self._bump_version(c)
            return cur.lastrowid

    def delete_goal(self, goal_id: int) -> bool:
        with self._conn() as c:
            cur = c.execute("UPDATE goals SET deleted = 1 WHERE id = ? AND deleted = 0", (goal_id,))
            if cur.rowcount:
                self._bump_version(c)
            return bool(cur.rowcount)

    # --- задачі ---------------------------------------------------------
    def add_task(self, hhmm: str, text: str, day: Optional[str] = None) -> int:
        """hhmm — місцевий час 'HH:MM'. Кидає ValueError при поганому форматі."""
        try:
            h, m = hhmm.split(":")
            local_time = time(int(h), int(m))
        except (ValueError, AttributeError):
            raise ValueError("Невірний час. Формат: ГГ:ХХ, наприклад 15:30")
        local_day = date.fromisoformat(day) if day else datetime.now(self.tz).date()
        local_dt = datetime.combine(local_day, local_time, tzinfo=self.tz)
        utc_str = local_dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        with self._conn() as c:
            cur = c.execute(
                "INSERT INTO tasks(day, time_utc, text, created_at) VALUES (?,?,?,?)",
                (local_day.isoformat(), utc_str, text.strip(), _utc_now()),
            )
            self._bump_version(c)
            return cur.lastrowid

    def mark_task_done(self, task_id: int) -> bool:
        with self._conn() as c:
            cur = c.execute(
                "UPDATE tasks SET status='DONE', done_at=? WHERE id=? AND deleted=0 AND status!='DONE'",
                (_utc_now(), task_id),
            )
            return bool(cur.rowcount)  # версію не піднімаємо: розклад на пристрої не змінився

    def delete_task(self, task_id: int) -> bool:
        with self._conn() as c:
            cur = c.execute("UPDATE tasks SET deleted = 1 WHERE id = ? AND deleted = 0", (task_id,))
            if cur.rowcount:
                self._bump_version(c)
            return bool(cur.rowcount)

    # --- розклад для пристрою (розділ 8.2) --------------------------------
    def get_schedule(self, day: Optional[str] = None) -> dict:
        day = day or self.today()
        with self._conn() as c:
            goals = c.execute(
                "SELECT id, title, minutes FROM goals WHERE day=? AND deleted=0 ORDER BY id", (day,)
            ).fetchall()
            tasks = c.execute(
                "SELECT id, time_utc, text FROM tasks "
                "WHERE day=? AND deleted=0 AND status!='DONE' ORDER BY time_utc", (day,)
            ).fetchall()
            version = int(c.execute("SELECT value FROM meta WHERE key='schedule_version'").fetchone()[0])
        return {
            "schedule_version": version,
            "date": day,
            "goals": [{"id": g["id"], "title": g["title"], "minutes": g["minutes"]} for g in goals],
            "tasks": [{"id": t["id"], "time": t["time_utc"], "text": t["text"]} for t in tasks],
        }

    # --- підсумки сесій (розділ 8.3) ---------------------------------------
    def add_session_summary(self, event: dict, device_id: Optional[str] = None) -> int:
        """event — JSON події FOCUS_DONE від пристрою."""
        with self._conn() as c:
            cur = c.execute(
                "INSERT INTO session_summaries(device_id, goal_id, title, minutes, paused_s, ts) "
                "VALUES (?,?,?,?,?,?)",
                (device_id, event.get("goal_id"), event["title"], int(event["minutes"]),
                 int(event.get("paused_s", 0)), event["ts"]),
            )
            return cur.lastrowid

    def list_summaries(self, day: Optional[str] = None) -> list[dict]:
        """Підсумки за місцеву дату (для /today і майбутнього /stats)."""
        day = day or self.today()
        start = datetime.combine(date.fromisoformat(day), time.min, tzinfo=self.tz).astimezone(timezone.utc)
        end = datetime.combine(date.fromisoformat(day), time.max, tzinfo=self.tz).astimezone(timezone.utc)
        fmt = "%Y-%m-%dT%H:%M:%SZ"
        with self._conn() as c:
            rows = c.execute(
                "SELECT * FROM session_summaries WHERE ts BETWEEN ? AND ? ORDER BY ts",
                (start.strftime(fmt), end.strftime(fmt)),
            ).fetchall()
        return [dict(r) for r in rows]
