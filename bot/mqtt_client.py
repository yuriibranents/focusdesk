"""
bot/mqtt_client.py — D4: MQTT-клієнт бота FocusDesk.

Що робить (розділ 8.1 ТЗ):
  * публікує розклад у  focusdesk/<id>/schedule   (QoS 1, retained)
  * слухає              focusdesk/<id>/event      (FOCUS_DONE, TASK_DONE, BREAK_*)
                        focusdesk/<id>/status     (online/offline, версія, heap)
                        focusdesk/<id>/ack        (підтвердження розкладу)
  * чекає ack на конкретну schedule_version, щоб бот міг чесно відповісти
    «збережено І надіслано на пристрій».

paho-mqtt працює у власному потоці (loop_start), а aiogram / python-telegram-bot —
в asyncio. Тому всі колбеки з paho перекидаються в asyncio-цикл через
loop.call_soon_threadsafe.

Залежності:  pip install "paho-mqtt>=2.0"
Змінні оточення (.env на сервері, НЕ в Git):
  MQTT_HOST=localhost  MQTT_PORT=1883  MQTT_USER=  MQTT_PASSWORD=
  MQTT_CLIENT_ID=focusdesk-bot  MQTT_TOPIC_ROOT=focusdesk

Припущення (узгодити з C у docs/protocol.md, завдання C8):
  * ack-повідомлення — JSON  {"schedule_version": 7}  (додаткові поля ігноруються);
  * status — або рядок "online"/"offline", або JSON {"state": "online", "fw": "...", "heap": N};
  * подія — JSON із розділу 8.3 (поля ts, type, ...).
"""

from __future__ import annotations

import asyncio
import inspect
import json
import logging
import os
from dataclasses import dataclass, field
from datetime import date, datetime, timezone
from typing import Any, Awaitable, Callable, Optional, Union

import paho.mqtt.client as mqtt

log = logging.getLogger("focusdesk.mqtt")

# --------------------------------------------------------------------------- #
# Конфігурація
# --------------------------------------------------------------------------- #


@dataclass
class MqttConfig:
    host: str = "localhost"
    port: int = 1883
    username: Optional[str] = None
    password: Optional[str] = None
    client_id: str = "focusdesk-bot"
    topic_root: str = "focusdesk"
    keepalive: int = 30  # узгоджено з ТЗ 7.5: keepalive 30 с, QoS 1

    @classmethod
    def from_env(cls) -> "MqttConfig":
        return cls(
            host=os.getenv("MQTT_HOST", "localhost"),
            port=int(os.getenv("MQTT_PORT", "1883")),
            username=os.getenv("MQTT_USER") or None,
            password=os.getenv("MQTT_PASSWORD") or None,
            client_id=os.getenv("MQTT_CLIENT_ID", "focusdesk-bot"),
            topic_root=os.getenv("MQTT_TOPIC_ROOT", "focusdesk"),
        )


# --------------------------------------------------------------------------- #
# Моделі повідомлень
# --------------------------------------------------------------------------- #


@dataclass
class DeviceEvent:
    """Подія від пристрою (розділ 8.3)."""

    device_id: str
    type: str  # FOCUS_DONE, TASK_DONE, BREAK_DUE, ...
    ts: Optional[datetime]  # UTC
    data: dict[str, Any] = field(default_factory=dict)  # решта полів JSON


@dataclass
class DeviceStatus:
    device_id: str
    online: bool
    fw: Optional[str] = None
    heap: Optional[int] = None
    raw: dict[str, Any] = field(default_factory=dict)


Handler = Callable[[Any], Union[None, Awaitable[None]]]


# --------------------------------------------------------------------------- #
# Допоміжні функції
# --------------------------------------------------------------------------- #


def to_utc_iso(dt: datetime) -> str:
    """datetime (з tzinfo!) -> '2026-10-20T12:30:00Z'. Формат із розділу 8.2."""
    if dt.tzinfo is None:
        raise ValueError("Потрібен datetime із часовим поясом (aware)")
    return dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_utc_iso(s: str) -> Optional[datetime]:
    try:
        return datetime.fromisoformat(s.replace("Z", "+00:00"))
    except (ValueError, AttributeError):
        return None


def build_schedule(
    schedule_version: int,
    day: date,
    goals: list[dict],
    tasks: list[dict],
) -> dict:
    """
    Збирає розклад у форматі розділу 8.2.

    goals: [{"id": 1, "title": "Англійська", "minutes": 120}]
    tasks: [{"id": 11, "time": <aware datetime | ISO-рядок з 'Z'>, "text": "Піти в магазин"}]
    """
    out_tasks = []
    for t in tasks:
        when = t["time"]
        out_tasks.append(
            {
                "id": t["id"],
                "time": to_utc_iso(when) if isinstance(when, datetime) else when,
                "text": t["text"],
            }
        )
    return {
        "schedule_version": schedule_version,
        "date": day.isoformat(),
        "goals": [
            {"id": g["id"], "title": g["title"], "minutes": g["minutes"]} for g in goals
        ],
        "tasks": out_tasks,
    }


def format_event_message(ev: DeviceEvent) -> Optional[str]:
    """Текст для Telegram за подією. None — повідомляти не треба."""
    d = ev.data
    if ev.type == "FOCUS_DONE":
        title = d.get("title", "—")
        minutes = d.get("minutes", "?")
        paused = d.get("paused_s")
        msg = f"✅ Фокус-сесію «{title}» завершено: {minutes} хв."
        if paused:
            msg += f"\nПауз сумарно: {round(paused / 60)} хв."
        return msg
    if ev.type == "TASK_DONE":
        text = d.get("text") or f"#{d.get('task_id', '?')}"
        return f"✔️ Задачу виконано: {text}"
    if ev.type == "BREAK_DUE":
        return "🚶 Час на перерву — ви сидите надто довго."
    if ev.type == "BREAK_DONE":
        return "☕ Перерву зараховано."
    return None  # решта подій — лише в лог


# --------------------------------------------------------------------------- #
# Клієнт
# --------------------------------------------------------------------------- #


class MqttBridge:
    """
    Міст «бот ⇄ MQTT». Використання:

        bridge = MqttBridge(MqttConfig.from_env())
        bridge.on_event(handle_event)      # async або sync функція(DeviceEvent)
        bridge.on_status(handle_status)    # (DeviceStatus)
        await bridge.start()               # у стартовому хуку бота
        ...
        acked = await bridge.publish_schedule("a1b2c3d4e5f6", schedule, wait_ack=5)
        ...
        await bridge.stop()
    """

    def __init__(self, cfg: Optional[MqttConfig] = None):
        self.cfg = cfg or MqttConfig.from_env()
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._connected: Optional[asyncio.Event] = None  # створюється в start()
        self._event_handlers: list[Handler] = []
        self._status_handlers: list[Handler] = []
        self._ack_handlers: list[Handler] = []
        # (device_id, schedule_version) -> Future; чіпається лише з потоку asyncio
        self._ack_waiters: dict[tuple[str, int], asyncio.Future] = {}
        self.device_status: dict[str, DeviceStatus] = {}

        # paho-mqtt 2.x. clean_session=False + фіксований client_id: брокер тримає
        # підписки й QoS1-повідомлення, поки бот перезапускається.
        self._client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=self.cfg.client_id,
            protocol=mqtt.MQTTv311,
            clean_session=False,
        )
        if self.cfg.username:
            self._client.username_pw_set(self.cfg.username, self.cfg.password)
        self._client.reconnect_delay_set(min_delay=1, max_delay=30)
        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message

    # ----------------------------- публічний API ----------------------------- #

    def on_event(self, handler: Handler) -> None:
        self._event_handlers.append(handler)

    def on_status(self, handler: Handler) -> None:
        self._status_handlers.append(handler)

    def on_ack(self, handler: Handler) -> None:
        self._ack_handlers.append(handler)

    @property
    def connected(self) -> bool:
        return self._client.is_connected()

    async def start(self, wait_connected: float = 5.0) -> bool:
        """
        Запускає мережевий потік. connect_async не кидає виняток, якщо брокер
        недоступний — paho сам перепідключається. Повертає True, якщо за
        wait_connected секунд з'єднання встановлено.
        """
        self._loop = asyncio.get_running_loop()
        self._connected = asyncio.Event()
        self._client.connect_async(self.cfg.host, self.cfg.port, self.cfg.keepalive)
        self._client.loop_start()
        try:
            await asyncio.wait_for(self._connected.wait(), wait_connected)
            return True
        except asyncio.TimeoutError:
            log.warning("MQTT: брокер недоступний, продовжуємо спроби у фоні")
            return False

    async def stop(self) -> None:
        self._client.disconnect()
        self._client.loop_stop()
        for fut in self._ack_waiters.values():
            if not fut.done():
                fut.cancel()
        self._ack_waiters.clear()

    async def publish_schedule(
        self,
        device_id: str,
        schedule: dict,
        wait_ack: Optional[float] = None,
    ) -> Optional[bool]:
        """
        Публікує розклад (retained, QoS 1).

        wait_ack=None  -> повертає None одразу після постановки в чергу;
        wait_ack=N     -> чекає ack із такою ж schedule_version до N секунд:
                          True  — пристрій підтвердив;
                          False — таймаут (пристрій офлайн; розклад лежить
                                  retained на брокері й дійде при підключенні).
        Повідомлення, опубліковане під час розриву з брокером, paho ставить у
        чергу й відправить після перепідключення.
        """
        version = int(schedule["schedule_version"])
        payload = json.dumps(schedule, ensure_ascii=False, separators=(",", ":"))

        fut: Optional[asyncio.Future] = None
        if wait_ack is not None:
            fut = self._loop.create_future()
            self._ack_waiters[(device_id, version)] = fut

        info = self._client.publish(
            self._topic(device_id, "schedule"), payload, qos=1, retain=True
        )
        if info.rc != mqtt.MQTT_ERR_SUCCESS and info.rc != mqtt.MQTT_ERR_NO_CONN:
            self._ack_waiters.pop((device_id, version), None)
            raise RuntimeError(f"MQTT publish не вдався, rc={info.rc}")
        log.info("MQTT: розклад v%s -> %s (%d байт)", version, device_id, len(payload))

        if fut is None:
            return None
        try:
            await asyncio.wait_for(fut, wait_ack)
            return True
        except asyncio.TimeoutError:
            return False
        finally:
            self._ack_waiters.pop((device_id, version), None)

    def clear_schedule(self, device_id: str) -> None:
        """Видаляє retained-розклад на брокері (порожній retained payload)."""
        self._client.publish(
            self._topic(device_id, "schedule"), b"", qos=1, retain=True
        )

    # ------------------------------- внутрішнє ------------------------------- #

    def _topic(self, device_id: str, leaf: str) -> str:
        return f"{self.cfg.topic_root}/{device_id}/{leaf}"

    # Колбеки paho — виконуються в потоці paho, не в asyncio!

    def _on_connect(self, client, userdata, flags, reason_code, properties=None):
        if reason_code != 0:  # у paho 2.x reason_code — ReasonCode, порівнюється з int
            log.error("MQTT: відмова підключення: %s", reason_code)
            return
        log.info("MQTT: підключено до %s:%s", self.cfg.host, self.cfg.port)
        # Підписуємось при кожному (пере)підключенні — так надійніше.
        for leaf in ("event", "status", "ack"):
            client.subscribe(f"{self.cfg.topic_root}/+/{leaf}", qos=1)
        if self._loop and self._connected:
            self._loop.call_soon_threadsafe(self._connected.set)

    def _on_disconnect(
        self, client, userdata, disconnect_flags, reason_code, properties=None
    ):
        log.warning(
            "MQTT: з'єднання втрачено (%s), paho перепідключається", reason_code
        )
        if self._loop and self._connected:
            self._loop.call_soon_threadsafe(self._connected.clear)

    def _on_message(self, client, userdata, msg):
        parts = msg.topic.split("/")
        if len(parts) != 3 or parts[0] != self.cfg.topic_root:
            return
        _, device_id, leaf = parts
        if not msg.payload:  # порожній retained = «очищено»
            return
        try:
            text = msg.payload.decode("utf-8")
        except UnicodeDecodeError:
            log.warning("MQTT: не UTF-8 у %s", msg.topic)
            return
        if self._loop:
            self._loop.call_soon_threadsafe(self._dispatch, device_id, leaf, text)

    # Далі — вже в потоці asyncio.

    def _dispatch(self, device_id: str, leaf: str, text: str) -> None:
        if leaf == "status":
            status = self._parse_status(device_id, text)
            self.device_status[device_id] = status
            self._call(self._status_handlers, status)
            return

        try:
            obj = json.loads(text)
        except json.JSONDecodeError:
            log.warning("MQTT: невалідний JSON у %s/%s: %.80s", device_id, leaf, text)
            return
        if not isinstance(obj, dict):
            log.warning("MQTT: очікувався JSON-об'єкт у %s/%s", device_id, leaf)
            return

        if leaf == "ack":
            version = obj.get("schedule_version")
            if isinstance(version, int):
                fut = self._ack_waiters.get((device_id, version))
                if fut and not fut.done():
                    fut.set_result(True)
            self._call(self._ack_handlers, (device_id, obj))
        elif leaf == "event":
            ev = DeviceEvent(
                device_id=device_id,
                type=str(obj.get("type", "UNKNOWN")),
                ts=parse_utc_iso(obj.get("ts", "")),
                data={k: v for k, v in obj.items() if k not in ("type", "ts")},
            )
            log.info("MQTT: подія %s від %s", ev.type, device_id)
            self._call(self._event_handlers, ev)

    @staticmethod
    def _parse_status(device_id: str, text: str) -> DeviceStatus:
        t = text.strip()
        if t.lower() in ("online", "offline"):
            return DeviceStatus(device_id, t.lower() == "online")
        try:
            obj = json.loads(t)
            state = str(obj.get("state", obj.get("status", ""))).lower()
            return DeviceStatus(
                device_id,
                state == "online",
                fw=obj.get("fw"),
                heap=obj.get("heap"),
                raw=obj,
            )
        except (json.JSONDecodeError, AttributeError):
            return DeviceStatus(device_id, False)

    def _call(self, handlers: list[Handler], arg: Any) -> None:
        for h in handlers:
            try:
                res = h(arg)
                if inspect.isawaitable(res):
                    task = asyncio.ensure_future(res)
                    task.add_done_callback(self._log_task_error)
            except Exception:  # колбек користувача не повинен ламати міст
                log.exception("MQTT: помилка в обробнику")

    @staticmethod
    def _log_task_error(task: asyncio.Future) -> None:
        if not task.cancelled() and task.exception():
            log.error("MQTT: помилка в async-обробнику", exc_info=task.exception())


# --------------------------------------------------------------------------- #
# Ручна перевірка:  python -m bot.mqtt_client <device_id>
# Піднімає міст, публікує тестовий розклад і друкує події/ack від пристрою
# (або від tools/device_emulator.py).
# --------------------------------------------------------------------------- #


async def _demo(device_id: str) -> None:
    from datetime import timedelta

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(message)s")
    bridge = MqttBridge()
    bridge.on_event(lambda ev: print("EVENT :", ev, "->", format_event_message(ev)))
    bridge.on_status(lambda st: print("STATUS:", st))

    if not await bridge.start():
        print("Брокер недоступний — запустіть Mosquitto (docs/deploy.md)")

    now = datetime.now(timezone.utc)
    schedule = build_schedule(
        schedule_version=int(now.timestamp()) % 100000,
        day=now.date(),
        goals=[{"id": 1, "title": "Англійська", "minutes": 120}],
        tasks=[
            {"id": 11, "time": now + timedelta(minutes=2), "text": "Піти в магазин"}
        ],
    )
    acked = await bridge.publish_schedule(device_id, schedule, wait_ack=10)
    print("ACK отримано" if acked else "ACK не отримано (пристрій офлайн?)")
    print("Слухаю події 60 с... (Ctrl+C — вихід)")
    try:
        await asyncio.sleep(60)
    finally:
        await bridge.stop()


if __name__ == "__main__":
    import sys

    asyncio.run(_demo(sys.argv[1] if len(sys.argv) > 1 else "emulator"))
