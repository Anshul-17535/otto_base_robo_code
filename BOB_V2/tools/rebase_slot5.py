#!/usr/bin/env python3
"""Rebase NVS slot 5 (Foot dance) from neutral [90,90,90,90] to [54,71,71,86]."""
import json, sys, urllib.request
sys.path.insert(0, "BOB_V2/tools")
from motion_client import MotionClient

BOB = "192.168.1.7"
old_n = [90, 90, 90, 90]
new_n = [54, 71, 71, 86]
def rebase(angles):
    return [max(0.0, min(180.0, new_n[i] + (angles[i] - old_n[i]))) for i in range(4)]

slot5 = {
  "slot": 5,
  "sequence": {
    "name": "Foot dance", "expression": 0, "mode": "joints", "version": 1,
    "frames": [
      {"angles": rebase([90,90,90,90]),   "moveMs": 300, "holdMs": 80},
      {"angles": rebase([95,108,95,108]), "moveMs": 240, "holdMs": 60},
      {"angles": rebase([90,128,90,55]),  "moveMs": 300, "holdMs": 70},
      {"angles": rebase([90,55,90,128]),  "moveMs": 300, "holdMs": 70},
      {"angles": rebase([90,128,90,55]),  "moveMs": 300, "holdMs": 70},
      {"angles": rebase([90,55,90,128]),  "moveMs": 300, "holdMs": 70},
      {"angles": rebase([93,126,93,126]), "moveMs": 260, "holdMs": 60},
      {"angles": rebase([87,56,87,56]),   "moveMs": 260, "holdMs": 60},
      {"angles": rebase([93,126,93,126]), "moveMs": 260, "holdMs": 60},
      {"angles": rebase([87,56,87,56]),   "moveMs": 260, "holdMs": 60},
      {"angles": rebase([116,120,64,66]), "moveMs": 340, "holdMs": 70},
      {"angles": rebase([64,66,116,120]), "moveMs": 340, "holdMs": 70},
      {"angles": rebase([96,130,96,130]), "moveMs": 360, "holdMs": 120},
      {"angles": rebase([90,90,90,90]),   "moveMs": 460, "holdMs": 200},
    ]
  }
}

robot = MotionClient(BOB)
with robot:
    print(f"Armed. Token: {robot.token}")
    payload = {**slot5, "token": robot.token}
    data = json.dumps(payload, allow_nan=False).encode()
    req = urllib.request.Request(
        f"http://{BOB}/motion/save", data=data,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as r:
        result = json.load(r)
    print("Slot 5 saved OK" if result.get("ok") else f"FAIL: {result}")
