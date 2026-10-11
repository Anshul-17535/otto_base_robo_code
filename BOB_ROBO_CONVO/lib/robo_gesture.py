#!/usr/bin/env python3
"""
robo_gesture.py — Rock/Paper/Scissors hand-gesture recognition for BOB_ROBO_CONVO

Hand it a JPEG (captured from the ESP32-CAM the instant the countdown hits "3")
and it returns the player's throw, using Google MediaPipe's pre-trained Gesture
Recognizer (Closed_Fist→rock, Open_Palm→paper, Victory→scissors).

Why a subprocess: mediapipe's only Python-3.14 wheel (1.0.x) crashes headless on
macOS, while the stable 0.10.x has no 3.14 wheel. So the recognizer runs in a
separate interpreter (.venv-gesture = Python 3.11 + mediapipe 0.10.x) — see
lib/gesture_worker.py — and this module talks to it over a stdin/stdout pipe.

Set up the worker venv once with:  ./setup_gesture.sh
"""
from __future__ import annotations

import base64
import json
import os
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

_HERE       = Path(__file__).parent
_MODEL_URL  = (
    "https://storage.googleapis.com/mediapipe-models/gesture_recognizer/"
    "gesture_recognizer/float16/1/gesture_recognizer.task"
)
_MODEL_PATH = _HERE / "models" / "gesture_recognizer.task"
_WORKER     = _HERE / "gesture_worker.py"
_PYTHON     = os.getenv("GESTURE_PYTHON",
                        str(_HERE.parent / ".venv-gesture" / "bin" / "python"))
_MIN_SCORE  = float(os.getenv("GESTURE_MIN_SCORE", "0.4"))

_proc: subprocess.Popen | None = None
_lock = threading.Lock()


def _log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] [gesture] {msg}", file=sys.stderr, flush=True)


def _ensure_model() -> None:
    if _MODEL_PATH.exists():
        return
    _MODEL_PATH.parent.mkdir(parents=True, exist_ok=True)
    _log(f"downloading gesture model (~8 MB) → {_MODEL_PATH.name} …")
    tmp = _MODEL_PATH.with_suffix(".task.part")
    urllib.request.urlretrieve(_MODEL_URL, tmp)
    tmp.rename(_MODEL_PATH)
    _log("model ready ✓")


def _start() -> subprocess.Popen | None:
    """Start (or reuse) the recognizer subprocess. Caller must hold _lock."""
    global _proc
    if _proc is not None and _proc.poll() is None:
        return _proc

    if not Path(_PYTHON).exists():
        _log(f"worker interpreter not found: {_PYTHON}")
        _log("run ./setup_gesture.sh to create the .venv-gesture (python3.11 + mediapipe)")
        return None
    try:
        _ensure_model()
    except Exception as exc:
        _log(f"model download failed: {exc}")
        return None

    _log("starting recognizer subprocess …")
    _proc = subprocess.Popen(
        [_PYTHON, str(_WORKER)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,   # mediapipe is very chatty — drop its logs
        text=True,
        bufsize=1,                   # line-buffered
    )
    # Wait for the {"ready": true} handshake (model load ~1-3 s)
    try:
        line = _proc.stdout.readline()
        if line and json.loads(line).get("ready"):
            _log("recognizer ready ✓")
            return _proc
    except Exception as exc:
        _log(f"worker handshake failed: {exc}")
    _log("worker did not report ready — disabling gesture recognition")
    try:
        _proc.kill()
    except Exception:
        pass
    _proc = None
    return None


def warm_up() -> None:
    """Pre-start the worker so the first game turn has no cold-start delay."""
    with _lock:
        _start()


def classify(jpeg_bytes: bytes) -> tuple[str | None, float]:
    """
    Classify a JPEG frame as a rock/paper/scissors throw.

    Returns (throw, score); throw is "rock"|"paper"|"scissors" or None when no
    hand / unrecognised gesture / low confidence / worker unavailable.
    """
    if not jpeg_bytes:
        return None, 0.0
    with _lock:
        p = _start()
        if p is None:
            return None, 0.0
        try:
            p.stdin.write(base64.b64encode(jpeg_bytes).decode() + "\n")
            p.stdin.flush()
            line = p.stdout.readline()
        except Exception as exc:
            _log(f"worker I/O error: {exc}")
            return None, 0.0
    if not line:
        _log("worker closed the pipe unexpectedly")
        return None, 0.0
    try:
        d = json.loads(line)
    except Exception:
        _log(f"bad worker reply: {line!r}")
        return None, 0.0

    throw = d.get("throw")
    score = float(d.get("score", 0.0))
    _log(f"{d.get('name','?')} ({score:.2f}) → {throw or 'unrecognised'}")
    if not throw or score < _MIN_SCORE:
        return None, score
    return throw, score


def decide(robot: str, human: str) -> str:
    """Return 'win' | 'lose' | 'draw' from the ROBOT's point of view."""
    if robot == human:
        return "draw"
    beats = {"rock": "scissors", "paper": "rock", "scissors": "paper"}
    return "win" if beats[robot] == human else "lose"
