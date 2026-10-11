#!/usr/bin/env bash
# setup_gesture.sh — create the hand-gesture recognizer venv for RPS.
#
# The main server runs on Python 3.14, but mediapipe's only 3.14 wheel (1.0.x)
# crashes headless on macOS. So gesture recognition runs in this SEPARATE venv
# using Python 3.11 + the stable mediapipe 0.10.x. server.py talks to it via a
# subprocess pipe (see lib/gesture_worker.py + lib/robo_gesture.py).
set -euo pipefail
cd "$(dirname "$0")"

PY="${GESTURE_BASE_PYTHON:-python3.11}"
if ! command -v "$PY" >/dev/null 2>&1; then
  echo "❌ $PY not found. Install it (e.g. 'brew install python@3.11') or set GESTURE_BASE_PYTHON." >&2
  exit 1
fi

echo "▶ Creating .venv-gesture with $($PY --version)"
"$PY" -m venv .venv-gesture
.venv-gesture/bin/python -m pip install --upgrade pip
echo "▶ Installing mediapipe 0.10.x + opencv (stable macOS CPU wheels)"
.venv-gesture/bin/python -m pip install "mediapipe>=0.10,<1.0" opencv-python numpy

echo "✓ Gesture venv ready. The recognizer model downloads automatically on first use."
