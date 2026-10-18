#!/usr/bin/env python3
"""
Fix the NVS: widen joint limits to fit Foot dance, then save slot 5.
Run from the workspace root: python3 BOB_V2/tools/fix_slot5_limits.py
"""
import json, sys, urllib.request, time
sys.path.insert(0, "BOB_V2/tools")
from motion_client import MotionClient

BOB = "192.168.1.7"
old_n = [90, 90, 90, 90]
new_n = [54, 71, 71, 86]

def rebase(angles):
    return [max(0.0, min(180.0, new_n[i] + (angles[i] - old_n[i]))) for i in range(4)]

# Widened joint limits: [low, high, pitch_mix, roll_mix]
# Expanded LF and RF to accommodate Foot dance's full swing while keeping neutral inside.
new_joints = [
    [24, 84, 0, 0],     # LH — unchanged
    [36, 111, 0, 0],    # LF — expanded from [41,101] to [36,111]
    [41, 101, 0, -1],   # RH — unchanged
    [51, 126, 0, -1],   # RF — expanded from [56,116] to [51,126]
]

# Current config from motion/state (gains, correction, slew, cutoff, neutral, pins)
current_config = {
    "gains": [[0,0,0],[0,0,0]],
    "joints": new_joints,
    "neutral": [54, 71, 71, 86],
    "customNeutral": True,
    "correction": 10,
    "slew": 40,
    "cutoff": 40,
}

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

def post(url, payload, timeout=10):
    data = json.dumps(payload, allow_nan=False).encode()
    req = urllib.request.Request(url, data=data,
                                  headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)

robot = MotionClient(BOB)
print(f"Arming session on {BOB}...")
with robot:
    print(f"Armed. Token: {robot.token}")

    # Step 1: widen joint limits and persist to NVS
    print("  Widening joint limits (LF: 41→36 lo, 101→111 hi; RF: 56→51 lo, 116→126 hi)...", end=" ", flush=True)
    result = post(f"http://{BOB}/motion/config",
                  {"token": robot.token, "config": current_config, "persist": True})
    print("OK" if result.get("ok") else f"FAIL: {result}")

    time.sleep(0.3)

    # Step 2: save slot 5 with the rebased angles
    print("  Saving slot 5: Foot dance (14 frames)...", end=" ", flush=True)
    result = post(f"http://{BOB}/motion/save",
                  {"token": robot.token, **slot5}, timeout=10)
    print("OK" if result.get("ok") else f"FAIL: {result}")

print("Done. Verifying...")
import urllib.request as ur
with ur.urlopen(f"http://{BOB}/motion/library", timeout=8) as r:
    lib = json.load(r)
s5 = lib["slots"][5]["sequence"]
first = s5["frames"][0]["angles"]
last  = s5["frames"][-1]["angles"]
print(f"Slot 5 first frame: {first}  (expected [54, 71, 71, 86])")
print(f"Slot 5 last frame:  {last}  (expected [54, 71, 71, 86])")
ok = (first == [54,71,71,86] and last == [54,71,71,86])
print("PASS" if ok else "FAIL — slot 5 still has old angles")
sys.exit(0 if ok else 1)
