"""FocusDesk device emulator (D5).

Імітує пристрій без плати: підключається до MQTT, приймає розклад,
підтверджує його (ack), запускає задачі за часом і фокус-сесії,
шле події боту. Теми й формати — розділ 8 технічного опису.

Запуск:
    pip install paho-mqtt
    python tools/device_emulator.py --broker localhost --device-id emu01

Команди в консолі:
    focus <goal_id>   старт фокус-сесії за цілью з розкладу
    lost / back       людина пішла / повернулась (пауза / відновлення)
    seen <task_id>    «бачу» (зупиняє нагадування)
    done <task_id>    «виконано» (шле TASK_DONE)
    status            поточний стан
    quit              вихід
"""

import argparse
import json
import threading
import time
from datetime import datetime, timezone

import paho.mqtt.client as mqtt

FW_VERSION = "emulator-0.1"


def now_utc():
    return datetime.now(timezone.utc)


def iso(dt):
    return dt.strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_iso(s):
    return datetime.fromisoformat(s.replace("Z", "+00:00"))


class Emulator:
    def __init__(self, args):
        self.args = args
        self.lock = threading.Lock()
        self.running = True

        # розклад і стани задач: SCHEDULED / DUE / SEEN / DONE
        self.schedule = {"schedule_version": 0, "goals": [], "tasks": []}
        self.task_status = {}
        self.due_at = {}  # task_id -> момент переходу в DUE (для затримки)

        # фокус-сесія: IDLE / RUNNING / PAUSED
        self.focus_state = "IDLE"
        self.goal = None
        self.remaining_s = 0.0
        self.worked_s = 0.0
        self.paused_s = 0.0
        self.present = True

        self.t_events = f"focusdesk/{args.device_id}/event"
        self.t_status = f"focusdesk/{args.device_id}/status"
        self.t_ack = f"focusdesk/{args.device_id}/ack"
        self.t_schedule = f"focusdesk/{args.device_id}/schedule"

        self.client = self._make_client()

    # ---------- MQTT ----------
    def _make_client(self):
        cid = f"focusdesk-{self.args.device_id}"
        try:
            c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=cid)
        except AttributeError:  # paho-mqtt 1.x
            c = mqtt.Client(client_id=cid)
        c.will_set(self.t_status, json.dumps({"state": "offline"}), qos=1, retain=True)
        c.on_connect = self.on_connect
        c.on_message = self.on_message
        c.reconnect_delay_set(min_delay=1, max_delay=10)
        return c

    def on_connect(self, client, userdata, flags, rc, props=None):
        print(f"[mqtt] підключено (rc={rc})")
        client.subscribe(self.t_schedule, qos=1)
        self.publish_status("online")

    def on_message(self, client, userdata, msg):
        try:
            data = json.loads(msg.payload.decode("utf-8"))
            version = int(data["schedule_version"])
            goals = data.get("goals", [])
            tasks = data.get("tasks", [])
        except (ValueError, KeyError, TypeError) as e:
            print(f"[schedule] некоректний JSON: {e!r}")
            return

        with self.lock:
            if version <= self.schedule["schedule_version"]:
                print(
                    f"[schedule] v{version} не новіша за "
                    f"v{self.schedule['schedule_version']}, ігнорую"
                )
                return
            self.schedule = data
            self.task_status = {t["id"]: "SCHEDULED" for t in tasks}
            self.due_at = {}
        print(
            f"[schedule] прийнято v{version}: "
            f"{len(goals)} цілей, {len(tasks)} задач"
        )
        client.publish(self.t_ack, json.dumps({"schedule_version": version}), qos=1)

    def publish_status(self, state):
        payload = {"state": state, "fw": FW_VERSION, "heap": 200000}
        self.client.publish(self.t_status, json.dumps(payload), qos=1, retain=True)

    def publish_event(self, ev_type, **fields):
        payload = {"ts": iso(now_utc()), "type": ev_type, **fields}
        self.client.publish(
            self.t_events,
            json.dumps(payload, ensure_ascii=False),
            qos=1,
        )
        print(f"[event] {payload}")

    # ---------- логіка (спрощена копія автоматів) ----------
    def tick(self, dt):
        with self.lock:
            self._tick_tasks()
            self._tick_focus(dt)

    def _tick_tasks(self):
        now = now_utc()
        for t in self.schedule.get("tasks", []):
            tid = t["id"]
            if self.task_status.get(tid) != "SCHEDULED":
                continue
            planned = parse_iso(t["time"])
            if now >= planned:
                self.task_status[tid] = "DUE"
                delay = (now - planned).total_seconds()
                print(
                    f"[task] #{tid} DUE «{t['text']}» "
                    f"(затримка {delay:.1f} с) — червоне мигання"
                )

    def _tick_focus(self, dt):
        if self.focus_state == "RUNNING":
            self.remaining_s -= dt
            self.worked_s += dt
            if self.remaining_s <= 0:
                self._finish_focus()
        elif self.focus_state == "PAUSED":
            self.paused_s += dt

    def _finish_focus(self):
        goal = self.goal
        self.focus_state = "IDLE"
        self.publish_event(
            "FOCUS_DONE",
            goal_id=goal["id"],
            title=goal["title"],
            minutes=round(self.worked_s / 60),
            paused_s=round(self.paused_s),
        )
        self.goal = None

    # ---------- команди користувача ----------
    def cmd_focus(self, goal_id):
        with self.lock:
            goal = next(
                (g for g in self.schedule.get("goals", []) if g["id"] == goal_id), None
            )
            if goal is None:
                print("немає такої цілі в розкладі")
                return
            if self.focus_state != "IDLE":
                print("сесія вже триває")
                return
            self.goal = goal
            self.remaining_s = goal["minutes"] * 60
            self.worked_s = 0.0
            self.paused_s = 0.0
            self.focus_state = "RUNNING" if self.present else "PAUSED"
            print(f"[focus] старт «{goal['title']}», {goal['minutes']} хв")

    def cmd_presence(self, present):
        with self.lock:
            self.present = present
            if present and self.focus_state == "PAUSED":
                self.focus_state = "RUNNING"
                print("[focus] відновлено")
            elif not present and self.focus_state == "RUNNING":
                self.focus_state = "PAUSED"
                print("[focus] пауза")
        print(f"[presence] {'є' if present else 'немає'}")

    def cmd_task(self, task_id, new_status):
        with self.lock:
            cur = self.task_status.get(task_id)
            allowed = {"SEEN": ("DUE",), "DONE": ("DUE", "SEEN")}
            if cur not in allowed[new_status]:
                print(f"задача #{task_id}: зі стану {cur} не можна в {new_status}")
                return
            self.task_status[task_id] = new_status
        print(f"[task] #{task_id} → {new_status}")
        if new_status == "DONE":
            self.publish_event("TASK_DONE", task_id=task_id)

    def cmd_status(self):
        with self.lock:
            print(
                f"розклад v{self.schedule['schedule_version']}, "
                f"сесія {self.focus_state}, "
                f"залишок {self.remaining_s:.0f} с, "
                f"задачі {self.task_status}"
            )

    # ---------- цикли ----------
    def tick_loop(self):
        while self.running:
            time.sleep(1)
            self.tick(self.args.speed)  # speed прискорює лише фокус-сесію

    def console_loop(self):
        while self.running:
            try:
                line = input("> ").strip().split()
            except EOFError:
                break
            if not line:
                continue
            cmd, rest = line[0], line[1:]
            try:
                if cmd == "focus":
                    self.cmd_focus(int(rest[0]))
                elif cmd == "lost":
                    self.cmd_presence(False)
                elif cmd == "back":
                    self.cmd_presence(True)
                elif cmd in ("seen", "done"):
                    self.cmd_task(int(rest[0]), cmd.upper())
                elif cmd == "status":
                    self.cmd_status()
                elif cmd == "quit":
                    break
                else:
                    print("невідома команда")
            except (IndexError, ValueError):
                print("потрібен числовий id, напр.: focus 1")
        self.running = False

    def run(self):
        self.client.connect(self.args.broker, self.args.port, keepalive=30)
        self.client.loop_start()
        threading.Thread(target=self.tick_loop, daemon=True).start()
        try:
            self.console_loop()
        except KeyboardInterrupt:
            pass
        finally:
            self.running = False
            self.publish_status("offline")
            time.sleep(0.3)
            self.client.loop_stop()
            self.client.disconnect()


def main():
    p = argparse.ArgumentParser(description="FocusDesk device emulator")
    p.add_argument("--broker", default="localhost")
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--device-id", default="emu01")
    p.add_argument(
        "--speed",
        type=float,
        default=1.0,
        help="множник часу для фокус-сесії (60 = хвилина за секунду)",
    )
    Emulator(p.parse_args()).run()


if __name__ == "__main__":
    main()
