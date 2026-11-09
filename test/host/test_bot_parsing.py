from datetime import datetime
from zoneinfo import ZoneInfo

import pytest

from bot.parsing import ParseError, parse_duration, parse_focus_args, parse_task_args

TZ = ZoneInfo("Europe/Kyiv")
NOW = datetime(2026, 10, 20, 12, 0, tzinfo=TZ)


@pytest.mark.parametrize("raw,exp", [
    ("2h", 120), ("90m", 90), ("1h30m", 90), ("2г", 120),
    ("90хв", 90), ("1год 30хв", 90), ("45", 45), ("2H", 120),
])
def test_duration_ok(raw, exp):
    assert parse_duration(raw) == exp


@pytest.mark.parametrize("raw", ["", "abc", "0", "0m", "11h", "-5", "2x"])
def test_duration_bad(raw):
    with pytest.raises(ParseError):
        parse_duration(raw)


def test_focus_multiword_title():
    assert parse_focus_args("Англійська мова 2h") == ("Англійська мова", 120)


@pytest.mark.parametrize("args", [None, "", "Англійська", "x" * 33 + " 2h"])
def test_focus_bad(args):
    with pytest.raises(ParseError):
        parse_focus_args(args)


def test_task_ok():
    assert parse_task_args("15:30 Піти в магазин", NOW, TZ) == ("15:30", "Піти в магазин")


def test_task_time_is_normalized():
    assert parse_task_args("9:05 x", datetime(2026, 10, 20, 8, 0, tzinfo=TZ), TZ)[0] == "09:05"


@pytest.mark.parametrize("args", [None, "15:30", "25:00 текст", "1530 текст",
                                  "11:00 минуле", "12:00 рівно зараз", "15:30 " + "x" * 33])
def test_task_bad(args):
    with pytest.raises(ParseError):
        parse_task_args(args, NOW, TZ)
