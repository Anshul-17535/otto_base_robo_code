#!/usr/bin/env python3
"""Local Motion Studio client. Default inspect command never moves the robot."""
from __future__ import annotations
import argparse
import json
import threading
import time
import urllib.request
from pathlib import Path


class MotionClient:
    def __init__(self, host: str):
        self.base = host.rstrip("/") if "://" in host else "http://" + host.rstrip("/")
        self.token = None
        self.error = None
        self._quit = threading.Event()
        self._thread = None
        self._http = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def request(self, path: str, payload=None):
        data = None if payload is None else json.dumps(payload, allow_nan=False).encode()
        req = urllib.request.Request(self.base + path, data=data,
                                     headers={"Content-Type": "application/json"})
        with self._http.open(req, timeout=1.2) as response:
            return json.load(response)

    def state(self):
        return self.request("/motion/state")

    def inspect(self):
        report = {}
        for path in ("/hw", "/state", "/imu", "/motion/state", "/motion/library"):
            try:
                report[path] = self.request(path)
            except Exception as exc:
                report[path] = {"error": str(exc)}
        return report

    def command(self, action: str, **payload):
        if self.error:
            raise RuntimeError("Heartbeat failed; controller stopped") from self.error
        if not self.token:
            raise RuntimeError("Use 'with MotionClient(host) as robot:' to arm a session")
        if action not in {"config", "pid", "play", "save", "delete"}:
            raise ValueError("Unknown motion action")
        return self.request("/motion/" + action, {**payload, "token": self.token})

    def _heartbeat(self):
        while not self._quit.wait(0.7):
            try:
                self.request("/motion/heartbeat", {"token": self.token})
            except Exception as exc:
                self.error = exc
                self._quit.set()
                break

    def __enter__(self):
        if self.token:
            raise RuntimeError("Session already armed")
        self.error = None
        self._quit.clear()
        self.token = self.request("/motion/arm", {})["token"]
        self._thread = threading.Thread(target=self._heartbeat, daemon=True)
        self._thread.start()
        return self

    def close(self):
        self._quit.set()
        if self._thread:
            self._thread.join(timeout=1.5)
        if self.token:
            try:
                # Token-scoped stop cannot interrupt a newer owner's session.
                self.request("/motion/stop", {"token": self.token})
            except Exception:
                pass  # the firmware also stops after the 3-second lease expires
        self.token = None

    def __exit__(self, *_):
        self.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="Bob's current IP or local hostname")
    sub = parser.add_subparsers(dest="action", required=True)
    inspect = sub.add_parser("inspect", help="Read hardware and motion status without arming")
    inspect.add_argument("--output", type=Path)
    play = sub.add_parser("play", help="Arm, play an exported sequence, then stop")
    play.add_argument("sequence", type=Path)
    play.add_argument("--seconds", type=float, default=20, help="Trial deadline, 1–120 seconds")
    play.add_argument("--pid", action="store_true", help="Enable already calibrated PID")
    play.add_argument("--telemetry", type=Path, help="Write sampled state as JSON lines")
    args = parser.parse_args()
    robot = MotionClient(args.host)
    if args.action == "inspect":
        report = json.dumps(robot.inspect(), indent=2)
        if args.output:
            args.output.write_text(report + "\n")
        else:
            print(report)
        return
    if not 1 <= args.seconds <= 120:
        parser.error("--seconds must be 1–120")
    sequence = json.loads(args.sequence.read_text())
    log = args.telemetry.open("w") if args.telemetry else None
    try:
        with robot:
            if args.pid:
                robot.command("pid", enabled=True, target=[0, 0])
            robot.command("play", sequence=sequence, repeats=1)
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                if robot.error:
                    raise RuntimeError("Heartbeat failed") from robot.error
                state = robot.state()
                state["recordedAt"] = time.time()
                if log:
                    log.write(json.dumps(state) + "\n")
                    log.flush()
                if not state["armed"]:
                    raise RuntimeError(state["reason"])
                if not state["playing"]:
                    print("Sequence finished; stopping and holding.")
                    break
                time.sleep(0.25)
            else:
                print("Trial deadline reached; stopping and holding.")
    finally:
        if log:
            log.close()


if __name__ == "__main__":
    main()
