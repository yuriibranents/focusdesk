import asyncio
from datetime import datetime
from zoneinfo import ZoneInfo

import pytest

from bot import commands
from bot.db import Database

TZ = ZoneInfo("Europe/Kyiv")
NOW = datetime(2026, 10, 20, 12, 0, tzinfo=TZ)  # 20.10.2026 ще літній час: UTC+3


@pytest.fixture
def db(tmp_path):
    return Database(str(tmp_path / "t.db"))


class FakePublish:
    def __init__(self, fail=False):
        self.fail = fail
        self.calls = []

    async def __call__(self, schedule):
        if self.fail:
            raise ConnectionError("broker down")
        self.calls.append(schedule)


def run(coro):
    return asyncio.run(coro)


def test_focus_saves_and_publishes(db):
    pub = FakePublish()
    reply = run(commands.focus(db, pub, "Англійська 2h", NOW))
    assert reply.startswith("✅") and "надіслано" in reply
    assert len(pub.calls) == 1
    sent = pub.calls[0]
    assert sent["date"] == "2026-10-20"
    assert sent["goals"][0]["title"] == "Англійська" and sent["goals"][0]["minutes"] == 120
    assert sent["schedule_version"] == db.schedule_version()


def test_focus_bad_args_changes_nothing(db):
    pub = FakePublish()
    v = db.schedule_version()
    reply = run(commands.focus(db, pub, "Англійська", NOW))
    assert reply.startswith("❌")
    assert pub.calls == [] and db.schedule_version() == v


def test_task_converts_to_utc(db):
    pub = FakePublish()
    reply = run(commands.task(db, pub, "15:30 Піти в магазин", NOW))
    assert reply.startswith("✅")
    assert pub.calls[0]["tasks"][0]["time"] == "2026-10-20T12:30:00Z"
    assert pub.calls[0]["tasks"][0]["text"] == "Піти в магазин"


def test_task_in_past_rejected(db):
    pub = FakePublish()
    reply = run(commands.task(db, pub, "11:00 x", NOW))
    assert reply.startswith("❌") and "минув" in reply
    assert pub.calls == [] and db.get_schedule("2026-10-20")["tasks"] == []


def test_publish_failure_keeps_data_and_says_so(db):
    reply = run(commands.task(db, FakePublish(fail=True), "15:30 x", NOW))
    assert reply.startswith("✅") and "⚠️" in reply and "не надіслано" in reply
    assert len(db.get_schedule("2026-10-20")["tasks"]) == 1


def test_today_empty(db):
    assert commands.today(db, NOW) == "На сьогодні нічого не заплановано."


def test_today_lists_goals_and_tasks_sorted_in_local_time(db):
    pub = FakePublish()
    run(commands.focus(db, pub, "Англійська 2h", NOW))
    run(commands.task(db, pub, "18:00 Пізніше", NOW))
    run(commands.task(db, pub, "15:30 Раніше", NOW))
    out = commands.today(db, NOW)
    assert "Англійська — 120 хв" in out
    assert out.index("15:30 — Раніше") < out.index("18:00 — Пізніше")


def test_today_hides_done_tasks(db):
    run(commands.task(db, FakePublish(), "15:30 x", NOW))
    db.mark_task_done(1)
    assert commands.today(db, NOW) == "На сьогодні нічого не заплановано."
