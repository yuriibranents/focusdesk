"""Імітація пристрою: python tools/mqtt_pub_test.py <device_id> event|ack [версія]"""

import json
import sys

import paho.mqtt.client as mqtt

device = sys.argv[1]
kind = sys.argv[2]

if kind == "event":
    payload = {
        "ts": "2026-10-20T14:05:11Z",
        "type": "FOCUS_DONE",
        "goal_id": 1,
        "title": "Англійська",
        "minutes": 118,
        "paused_s": 240,
    }
elif kind == "ack":
    payload = {"schedule_version": int(sys.argv[3])}
else:
    sys.exit("Другий аргумент: event або ack")

c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
c.connect("localhost", 1883)
c.loop_start()
info = c.publish(
    f"focusdesk/{device}/{kind}", json.dumps(payload, ensure_ascii=False), qos=1
)
info.wait_for_publish()
c.loop_stop()
c.disconnect()
print("Надіслано:", kind, payload)
