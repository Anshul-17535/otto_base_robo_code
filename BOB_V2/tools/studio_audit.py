"""Bounded API-v2 tabletop audit, called explicitly by tabletop_jog.py."""
import json
import time
from urllib.error import HTTPError

from motion_client import MotionClient
from prepare_tabletop_library import build_library


def run(host, pose, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    log = output.open("x")
    report = {"pose": pose, "samples": [], "checks": [], "completed": False,
              "physicalMovementVerified": False, "saved": []}
    robot = MotionClient(host)

    def state():
        current = robot.state()
        report["samples"].append({"recordedAt": time.time(), **current})
        return current

    def rejected(path, body, code):
        try:
            robot.request(path, body)
        except HTTPError as exc:
            if exc.code != code:
                raise RuntimeError(f"{path}: expected {code}, got {exc.code}") from exc
            report["checks"].append(path + " rejected as expected")
        else:
            raise RuntimeError(path + " unexpectedly accepted")

    try:
        initial = state()
        if initial.get("apiVersion", 0) < 2 or initial["armed"]:
            raise RuntimeError("API v2 and an unowned robot required")
        if (initial["commanded"] != pose or initial["config"]["neutral"] != pose or
                not initial["config"]["customNeutral"]):
            raise RuntimeError("Saved idle and actual command must match the requested pose")
        if (not initial["calibrated"] or not initial["imuSampleValid"] or
                initial["sampleAgeMs"] > 200 or
                max(abs(initial["pitch"]), abs(initial["roll"])) > 3):
            raise RuntimeError("A fresh, still calibrated IMU is required")
        library_before = robot.request("/motion/library")
        report["libraryBefore"] = library_before
        if any(slot["sequence"] is not None for slot in library_before["slots"]):
            raise RuntimeError("Preserve existing slots; this initial audit requires empty slots")
        sequence = {"name": "Four joint small test", "expression": 6, "frames": []}
        # Test one joint at a time, +2 then -2 degrees, with a neutral return.
        for channel in (3, 1, 0, 2):
            for delta in (2, -2, 0):
                angles = pose[:]
                angles[channel] += delta
                sequence["frames"].append({"angles": angles, "moveMs": 1200, "holdMs": 700})
        report["sequence"] = sequence
        with robot:
            holding = state()
            if holding["commanded"] != pose or holding["pid"]:
                raise RuntimeError("Arming changed pose or enabled PID")
            report["checks"].append("arm holds custom idle; PID off")
            rejected("/motion/heartbeat", {"token": "wrong-owner"}, 409)
            rejected("/motion/play", {"token": robot.token, "sequence": {
                "name": "invalid frame test", "frames": []}, "repeats": 1}, 400)
            # A legacy write to the SAME angle must still be blocked while owned.
            rejected(f"/servopos?ch=0&deg={pose[0]}", None, 409)
            robot.command("play", sequence=sequence, repeats=1)
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                current = state()
                if robot.error or not current["armed"]:
                    raise RuntimeError("Trial stopped: " + current["reason"])
                if (not current["imuSampleValid"] or not current["calibrated"] or
                        max(abs(current["pitch"]), abs(current["roll"])) > 5):
                    raise RuntimeError("Small-trial IMU envelope exceeded")
                if current["pid"] or any(abs(a - b) > 2 for a, b in zip(current["commanded"], pose)):
                    raise RuntimeError("Command left the two-degree test envelope")
                if not current["playing"]:
                    break
                time.sleep(.25)
            else:
                raise RuntimeError("Sequence deadline exceeded")
            if current["commanded"] != pose:
                raise RuntimeError("Sequence did not return to idle")
            report["checks"].append("12-frame four-joint trial completed and returned to idle")
            library = build_library(pose)
            for slot, (key, document) in enumerate(library.items()):
                robot.command("save", slot=slot, sequence=document)
                report["saved"].append({"slot": slot, "key": key, "sequence": document})
            saved = robot.request("/motion/library")["slots"]
            for entry in report["saved"]:
                actual = saved[entry["slot"]]["sequence"]
                if actual != entry["sequence"]:
                    raise RuntimeError("Saved sequence readback mismatch")
            report["checks"].append("six NVS slots saved and read back exactly")
        final = state()
        if final["armed"] or final["pid"] or final["commanded"] != pose:
            raise RuntimeError("Stop did not release ownership and hold idle")
        report["checks"].append("stop releases ownership and holds idle")
        report["completed"] = True
        print(json.dumps({"completed": True, "checks": report["checks"],
                          "savedSlots": len(report["saved"])}))
    except Exception as exc:
        report["error"] = str(exc)
        print(json.dumps({"completed": False, "error": str(exc), "checks": report["checks"]}))
        raise
    finally:
        json.dump(report, log, indent=2, allow_nan=False)
        log.write("\n")
        log.close()
