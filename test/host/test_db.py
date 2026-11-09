import json
import pytest
from bot.db import Database


@pytest.fixture
def db(tmp_path):
    return Database(str(tmp_path / "t.db"))


def test_schedule_matches_protocol(db):
    gid = db.add_goal("Англійська", 120)
    tid = db.add_task("12:30", "Піти в магазин")
    s = db.get_schedule()
    assert set(s) == {"schedule_version", "date", "goals", "tasks"}
    assert s["goals"] == [{"id": gid, "title": "Англійська", "minutes": 120}]
    assert s["tasks"][0]["id"] == tid and s["tasks"][0]["time"].endswith("Z")
    json.dumps(s, ensure_ascii=False)  # серіалізується для MQTT


def test_version_bumps_on_change_only(db):
    v0 = db.schedule_version()
    t = db.add_task("10:00", "x")
    v1 = db.schedule_version()
    assert v1 == v0 + 1
    db.mark_task_done(t)               # не змінює розклад пристрою
    assert db.schedule_version() == v1
    db.delete_task(t)
    assert db.schedule_version() == v1 + 1


def test_done_tasks_not_in_schedule(db):
    t = db.add_task("10:00", "x")
    db.mark_task_done(t)
    assert db.get_schedule()["tasks"] == []


def test_bad_time_and_long_text(db):
    with pytest.raises(ValueError):
        db.add_task("25:99", "x")
    with pytest.raises(ValueError):
        db.add_task("abc", "x")
    with pytest.raises(Exception):
        db.add_task("10:00", "я" * 33)   # ліміт 32 символи (ui_cmd_t)


def test_summary_roundtrip(db):
    ev = {"ts": "2026-10-20T14:05:11Z", "type": "FOCUS_DONE", "goal_id": None,
          "title": "Англійська", "minutes": 118, "paused_s": 240}
    db.add_session_summary(ev, device_id="aabbcc")
    assert db.list_summaries("2026-10-20")[0]["minutes"] == 118
