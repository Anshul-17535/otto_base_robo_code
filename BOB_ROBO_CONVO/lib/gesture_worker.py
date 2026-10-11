#!/usr/bin/env python3
"""
gesture_worker.py — MediaPipe hand-gesture recognizer subprocess.

Runs under the .venv-gesture interpreter (Python 3.11 + mediapipe 0.10.x).
It's a SEPARATE process because mediapipe's only Python-3.14 wheel (1.0.x)
crashes headless on macOS ("DrishtiMetalHelper … Service is unavailable"),
while 0.10.x is stable on CPU but has no 3.14 wheel.

Protocol (line-based, over stdin/stdout):
  • On startup, prints one JSON line:  {"ready": true}
  • For each input line (base64-encoded JPEG), prints one JSON result line:
        {"throw": "rock"|"paper"|"scissors"|null, "score": <float>, "name": "<gesture>"}

All mediapipe log spam goes to stderr, so stdout stays clean for the protocol.
"""
import base64
import json
import os
import sys
from pathlib import Path

import cv2
import numpy as np
import mediapipe as mp
from mediapipe.tasks import python
from mediapipe.tasks.python import vision

MODEL = Path(__file__).parent / "models" / "gesture_recognizer.task"

# MediaPipe canned gesture → RPS throw
MAP = {"Closed_Fist": "rock", "Open_Palm": "paper", "Victory": "scissors"}

# CAM_ROTATE: degrees to rotate every frame before classification.
# 180  → camera is physically mounted upside-down (proven correct for this mount)
#   0  → camera is right-side-up
_CAM_ROTATE = int(os.getenv("CAM_ROTATE", "180"))
_ROTATE_CODE = {
    90:  cv2.ROTATE_90_CLOCKWISE,
    180: cv2.ROTATE_180,
    270: cv2.ROTATE_90_COUNTERCLOCKWISE,
}.get(_CAM_ROTATE)   # None → no rotation


def _out(obj: dict) -> None:
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def main() -> None:
    base = python.BaseOptions(
        model_asset_path=str(MODEL),
        delegate=python.BaseOptions.Delegate.CPU,   # GPU/Metal delegate is unreliable headless
    )
    opts = vision.GestureRecognizerOptions(
        base_options=base,
        num_hands=1,
        running_mode=vision.RunningMode.IMAGE,
    )
    rec = vision.GestureRecognizer.create_from_options(opts)
    _out({"ready": True})

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            jpg = base64.b64decode(line)
            arr = np.frombuffer(jpg, np.uint8)
            bgr = cv2.imdecode(arr, cv2.IMREAD_COLOR)
            if bgr is None:
                _out({"throw": None, "score": 0.0, "name": "decode_error"})
                continue
            if _ROTATE_CODE is not None:
                bgr = cv2.rotate(bgr, _ROTATE_CODE)
            rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
            image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
            res = rec.recognize(image)
            if not res.gestures:
                _out({"throw": None, "score": 0.0, "name": "none"})
                continue
            top = res.gestures[0][0]
            _out({"throw": MAP.get(top.category_name),
                  "score": float(top.score),
                  "name": top.category_name})
        except Exception as exc:  # never die on a bad frame
            _out({"throw": None, "score": 0.0, "name": "error", "err": str(exc)})


if __name__ == "__main__":
    main()
