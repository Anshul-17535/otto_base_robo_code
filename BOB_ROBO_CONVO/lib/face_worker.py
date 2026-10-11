#!/usr/bin/env python3
"""
face_worker.py — MediaPipe face-detector subprocess.

Runs under the same .venv-gesture interpreter (Python 3.11 + mediapipe 0.10.x)
as gesture_worker.py, for the same reason (mediapipe's 3.14 wheel crashes
headless on macOS). It's a SEPARATE process from the RPS gesture worker so
presence polling and the RPS game never interfere with each other.

Protocol (line-based, over stdin/stdout):
  • On startup, prints one JSON line:  {"ready": true}
  • For each input line (base64-encoded JPEG), prints one JSON result line:
        {"faces": <int>, "score": <float>, "annotated": "<base64 JPEG>"}
    "annotated" is the (rotation-corrected) frame with detection boxes drawn on
    it, so the server dashboard can show "MediaPipe running" without re-doing any
    image work. It is "" if re-encoding fails.

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

MODEL = Path(__file__).parent / "models" / "blaze_face_short_range.tflite"

_MIN_SCORE = float(os.getenv("FACE_MIN_SCORE", "0.65"))

# Match the RPS worker: the camera is physically mounted upside-down on this rig.
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
    opts = vision.FaceDetectorOptions(
        base_options=base,
        min_detection_confidence=_MIN_SCORE,
        running_mode=vision.RunningMode.IMAGE,
    )
    det = vision.FaceDetector.create_from_options(opts)
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
                _out({"faces": 0, "score": 0.0, "name": "decode_error"})
                continue
            if _ROTATE_CODE is not None:
                bgr = cv2.rotate(bgr, _ROTATE_CODE)
            rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
            image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
            res = det.detect(image)
            dets = res.detections or []
            score = 0.0
            # Draw MediaPipe detection boxes onto the (rotated) frame so the
            # dashboard can display exactly what the detector sees. Bob-green
            # rectangles + a confidence label, matching the web UI theme.
            for d in dets:
                sc = float(d.categories[0].score) if d.categories else 0.0
                score = max(score, sc)
                bb = d.bounding_box
                x, y, w, h = bb.origin_x, bb.origin_y, bb.width, bb.height
                cv2.rectangle(bgr, (x, y), (x + w, y + h), (198, 228, 134), 2)
                cv2.putText(bgr, f"face {sc:.2f}", (x, max(14, y - 6)),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (198, 228, 134), 1,
                            cv2.LINE_AA)
            annotated = ""
            ok, buf = cv2.imencode(".jpg", bgr, [cv2.IMWRITE_JPEG_QUALITY, 70])
            if ok:
                annotated = base64.b64encode(buf.tobytes()).decode()
            _out({"faces": len(dets), "score": score, "annotated": annotated})
        except Exception as exc:  # never die on a bad frame
            _out({"faces": 0, "score": 0.0, "name": "error", "err": str(exc)})


if __name__ == "__main__":
    main()
