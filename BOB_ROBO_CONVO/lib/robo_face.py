#!/usr/bin/env python3
"""
robo_face.py — Face / person presence detection for BOB_ROBO_CONVO idle mode.

Hand it a JPEG (from the ESP32 built-in camera, GET /cam/snapshot) and it
returns how many faces are visible, using Google MediaPipe's BlazeFace short
range detector. Idle mode polls this to greet a person who walks up ("Hi, I am
Bob!") and say goodbye when they leave.

Same subprocess trick as robo_gesture.py: mediapipe's Python-3.14 wheel crashes
headless on macOS, so detection runs in the .venv-gesture interpreter
(Python 3.11 + mediapipe 0.10.x) — see lib/face_worker.py — talked to over a
stdin/stdout pipe. This is a SEPARATE process from the RPS gesture worker so the
two never block each other.

The worker venv is created by ./setup_gesture.sh (shared with RPS). The face
model auto-downloads on first use.
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
    "https://storage.googleapis.com/mediapipe-models/face_detector/"
    "blaze_face_short_range/float16/1/blaze_face_short_range.tflite"
)
_MODEL_PATH = _HERE / "models" / "blaze_face_short_range.tflite"
_WORKER     = _HERE / "face_worker.py"
_PYTHON     = os.getenv("GESTURE_PYTHON",
                        str(_HERE.parent / ".venv-gesture" / "bin" / "python"))

_proc: subprocess.Popen | None = None
_lock = threading.Lock()


def _log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] [face] {msg}", file=sys.stderr, flush=True)


def _ensure_model() -> None:
    if _MODEL_PATH.exists():
        return
    _MODEL_PATH.parent.mkdir(parents=True, exist_ok=True)
    _log(f"downloading face model (~230 KB) → {_MODEL_PATH.name} …")
    tmp = _MODEL_PATH.with_suffix(".tflite.part")
    urllib.request.urlretrieve(_MODEL_URL, tmp)
    tmp.rename(_MODEL_PATH)
    _log("model ready ✓")


def _start() -> subprocess.Popen | None:
    """Start (or reuse) the detector subprocess. Caller must hold _lock."""
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

    _log("starting face detector subprocess …")
    _proc = subprocess.Popen(
        [_PYTHON, str(_WORKER)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,   # mediapipe is very chatty — drop its logs
        text=True,
        bufsize=1,                   # line-buffered
    )
    try:
        line = _proc.stdout.readline()
        if line and json.loads(line).get("ready"):
            _log("face detector ready ✓")
            return _proc
    except Exception as exc:
        _log(f"worker handshake failed: {exc}")
    _log("worker did not report ready — disabling face detection")
    try:
        _proc.kill()
    except Exception:
        pass
    _proc = None
    return None


def warm_up() -> bool:
    """Pre-start the worker. Returns True if the detector is available."""
    with _lock:
        return _start() is not None


def detect(jpeg_bytes: bytes) -> int:
    """Return the number of faces visible in the JPEG (0 on any failure)."""
    if not jpeg_bytes:
        return 0
    with _lock:
        p = _start()
        if p is None:
            return 0
        try:
            p.stdin.write(base64.b64encode(jpeg_bytes).decode() + "\n")
            p.stdin.flush()
            line = p.stdout.readline()
        except Exception as exc:
            _log(f"worker I/O error: {exc}")
            return 0
    if not line:
        _log("worker closed the pipe unexpectedly")
        return 0
    try:
        return int(json.loads(line).get("faces", 0))
    except Exception:
        _log(f"bad worker reply: {line!r}")
        return 0


def detect_annotated(jpeg_bytes: bytes) -> tuple[int, bytes | None]:
    """Detect faces and return (count, annotated_jpeg).

    Same as detect() but also returns the frame with MediaPipe boxes drawn on
    it (rotation-corrected by the worker), ready to serve on the web dashboard.
    annotated is None if the worker is unavailable or gave no image — callers
    should fall back to the raw frame so the feed still shows something.
    """
    if not jpeg_bytes:
        return 0, None
    with _lock:
        p = _start()
        if p is None:
            return 0, None
        try:
            p.stdin.write(base64.b64encode(jpeg_bytes).decode() + "\n")
            p.stdin.flush()
            line = p.stdout.readline()
        except Exception as exc:
            _log(f"worker I/O error: {exc}")
            return 0, None
    if not line:
        _log("worker closed the pipe unexpectedly")
        return 0, None
    try:
        d = json.loads(line)
        faces = int(d.get("faces", 0))
        ann_b64 = d.get("annotated", "")
        annotated = base64.b64decode(ann_b64) if ann_b64 else None
        return faces, annotated
    except Exception:
        _log(f"bad worker reply: {line[:80]!r}")
        return 0, None
