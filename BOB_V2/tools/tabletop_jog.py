#!/usr/bin/env python3
"""Explicit one-degree, one-joint trial for legacy Bob firmware. No gait or PID."""
import argparse
import json
import math
import statistics
import time
import urllib.request
from pathlib import Path


def check_state(state, expected, baseline=None):
    if state["armed"] or state["pid"] or state["playing"]:
        raise RuntimeError("Another controller is active")
    if state["commanded"] != expected:
        raise RuntimeError("Pose changed unexpectedly; holding current command")
    if (not state["imuReady"] or state.get("imuSampleValid") is False or
            not state["calibrated"] or state["sampleAgeMs"] > 200):
        raise RuntimeError("IMU missing, uncalibrated or stale")
    angles = [state["pitch"], state["roll"]]
    if any(not math.isfinite(v) or abs(v) > 8 for v in angles):
        raise RuntimeError("Tilt exceeds small-trial envelope")
    if baseline and any(abs(v - b) > 3 for v, b in zip(angles, baseline)):
        raise RuntimeError("Tilt changed more than 3 degrees; holding current command")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--pose", nargs=4, type=int, required=True)
    parser.add_argument("--channel", type=int, choices=range(4), required=True)
    parser.add_argument("--delta", type=int, choices=(-1, 1), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--studio-check", action="store_true", help="API v2: four joints within two degrees, save six slots, verify stop")
    parser.add_argument("--observe-only", action="store_true", help="Record 30 raw IMU/status pairs; never send a write")
    args = parser.parse_args()
    if any(v < 2 or v > 178 for v in args.pose):
        parser.error("Pose must allow a one-degree trial inside servo range")
    if args.studio_check:
        from studio_audit import run
        return run(args.host, args.pose, args.output)
    # Refuse to overwrite previous evidence. Open before any robot action.
    args.output.parent.mkdir(parents=True, exist_ok=True)
    log = args.output.open("x")
    report = {"pose": args.pose, "channel": args.channel, "delta": args.delta,
              "samples": [], "completed": False, "returned": False}
    http = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def get(path):
        with http.open("http://" + args.host + path, timeout=1.5) as response:
            return json.load(response)

    expected = args.pose[:]
    baseline = None

    def observe(phase, count):
        samples = []
        for _ in range(count):
            state = get("/motion/state")
            report["samples"].append({"time": time.time(), "phase": phase, **state})
            check_state(state, expected, baseline)
            samples.append(state)
            time.sleep(.25)
        return samples

    try:
        if args.observe_only:
            for _ in range(30):
                report["samples"].append({"time": time.time(), "phase": "read_only",
                                          "state": get("/motion/state"), "imu": get("/imu")})
                time.sleep(.25)
            report["observationComplete"] = True
            print("Recorded 30 read-only IMU/status pairs; no motor commands")
            return
        before = observe("baseline", 6)
        baseline = [statistics.mean(s[key] for s in before) for key in ("pitch", "roll")]
        if any(max(s[key] for s in before) - min(s[key] for s in before) > 1.5
               for key in ("pitch", "roll")):
            raise RuntimeError("Robot is not still enough for a trial")
        report["baseline"] = baseline
        report["liftBefore"] = get("/liftdetect")
        # Prevent an automatic 90-degree-based kick during this legacy trial.
        # Leave disabled for tabletop work; re-enabling also recalibrates the IMU.
        disabled = get("/liftdetect?on=0")
        if disabled["enabled"]:
            raise RuntimeError("Could not disable automatic lift kicks")
        observe("preflight", 1)
        expected[args.channel] += args.delta
        reply = get(f"/servopos?ch={args.channel}&deg={expected[args.channel]}")
        if reply["pos"] != expected:
            raise RuntimeError("Unexpected command acknowledgement")
        report["offsetCommandAcknowledged"] = True
        displaced = observe("offset", 8)
        report["response"] = [statistics.mean(s[key] for s in displaced[-4:]) - b
                              for key, b in zip(("pitch", "roll"), baseline)]
        # Return only after observing a healthy, unchanged pose. On a fault or
        # lost connection no blind recovery motion is sent; hold at <=1 degree.
        expected = args.pose[:]
        reply = get(f"/servopos?ch={args.channel}&deg={expected[args.channel]}")
        if reply["pos"] != expected:
            raise RuntimeError("Unexpected return acknowledgement")
        report["returnCommandAcknowledged"] = True
        observe("returned", 6)
        report["returned"] = report["completed"] = True
        print(json.dumps({key: report[key] for key in
                          ("completed", "returned", "baseline", "response")}))
    except Exception as exc:
        report["error"] = str(exc)
        raise
    finally:
        json.dump(report, log, indent=2, allow_nan=False)
        log.write("\n")
        log.close()


if __name__ == "__main__":
    main()
