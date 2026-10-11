#!/usr/bin/env python3
"""
robo_robot.py — Robot HTTP helpers for BOB_ROBO_CONVO
Direct HTTP to the ESP32. All functions are fire-and-forget.

BOB V2 note: the OV2640 camera is now built into the main ESP32-S3 firmware.
There is no longer a separate ESP32-CAM board. Camera endpoints are:
  GET http://<ROBOT_HOST>/cam/snapshot  → single JPEG (used for RPS)
  GET http://<ROBOT_HOST>:81/stream     → MJPEG live stream
CAM_HOST is no longer used; cam_capture() calls ROBOT_HOST directly.
"""
from __future__ import annotations

import os
from contextlib import contextmanager
import io
import wave
import threading
import sys
import time
import urllib.parse
from pathlib import Path

import requests
from dotenv import load_dotenv

load_dotenv(dotenv_path=Path(__file__).parent.parent / ".env", override=True)
load_dotenv(dotenv_path=Path(__file__).parent.parent.parent / "BOB_ASSISTANT" / ".env", override=False)

ROBOT_HOST = os.getenv("ROBOT_HOST", "192.168.1.12").strip().rstrip("/")
ROBOT_BASE_URL = ROBOT_HOST if "://" in ROBOT_HOST else f"http://{ROBOT_HOST}"
ROBOT_HOST = urllib.parse.urlsplit(ROBOT_BASE_URL).netloc
IMU_ENABLED = os.getenv("ROBOT_IMU_ENABLED", "1").strip() == "1"
# The ESP32 WebServer handles recording and playback synchronously.
_HTTP_LOCK = threading.RLock()

# Set while TTS audio is streaming to the robot. The camera/vision thread checks
# this and PAUSES grabbing frames so the whole WiFi link is free for audio —
# otherwise camera traffic starves the audio stream and it breaks up.
AUDIO_BUSY = threading.Event()


# Foreground work owns a whole interaction, not just one HTTP request. Pending
# owners prevent idle from starting another move while the current one drains.
_ACTIVITY_LOCK = threading.RLock()
_ACTIVITY_STATE = threading.Lock()
_ACTIVITY_WAITERS = 0
FOREGROUND_BUSY = threading.Event()

@contextmanager
def foreground_activity():
    global _ACTIVITY_WAITERS
    with _ACTIVITY_STATE:
        _ACTIVITY_WAITERS += 1
        FOREGROUND_BUSY.set()
    try:
        with _ACTIVITY_LOCK:
            yield
    finally:
        with _ACTIVITY_STATE:
            _ACTIVITY_WAITERS -= 1
            if not _ACTIVITY_WAITERS:
                FOREGROUND_BUSY.clear()

@contextmanager
def idle_activity():
    with _ACTIVITY_STATE:
        acquired = not _ACTIVITY_WAITERS and _ACTIVITY_LOCK.acquire(blocking=False)
    try:
        yield acquired
    finally:
        if acquired:
            _ACTIVITY_LOCK.release()


def robot_request(method: str, path: str, **kwargs):
    with _HTTP_LOCK:
        response = requests.request(method, ROBOT_BASE_URL + path, **kwargs)
        if not response.ok:
            raise requests.HTTPError(
                f"{path}: HTTP {response.status_code}: {response.text[:200]}",
                response=response,
            )
        return response


def capture_audio(out_path: str, secs: int = 3) -> bool:
    """Download the S3's bounded 16 kHz mono PCM capture; reject invalid WAVs."""
    secs = max(1, min(3, int(secs)))
    try:
        response = robot_request("GET", "/record", params={"secs": secs},
                                 timeout=(5, secs + 10))
        with wave.open(io.BytesIO(response.content), "rb") as wav:
            if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(),
                    wav.getcomptype()) != (1, 2, 16000, "NONE"):
                raise ValueError("expected 16 kHz mono PCM16 WAV")
            frames = wav.getnframes()
            if not 0 < frames <= 3 * 16000 or len(wav.readframes(frames)) != frames * 2:
                raise ValueError("empty, oversized, or truncated recording")
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)
        Path(out_path).write_bytes(response.content)
        return True
    except Exception as exc:
        _log(f"mic capture failed: {exc}")
        return False

# CAM_HOST is kept for reference but no longer used — camera is on ROBOT_HOST.
CAM_HOST   = os.getenv("CAM_HOST", ROBOT_HOST)

EXPR: dict[str, int] = {
    "happy":0, "sad":1, "angry":2, "surprised":3,
    "sleepy":4, "love":5, "normal":6,
}
VALID_MOVES = {
    "fwd","bwd","left","right","stand",
    "dance","dance0","dance1","dance2","dance3","dance4",
}


def _log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] [robot] {msg}", file=sys.stderr, flush=True)


def robot_get(path: str, timeout: int = 10) -> bool:
    try:
        r = robot_request("GET", path, timeout=timeout)
        if r.status_code != 200:
            _log(f"GET {path} → HTTP {r.status_code}")
            return False
        return True
    except Exception as exc:
        _log(f"GET {path} failed: {exc}")
        return False


def set_expression(expr: str) -> None:
    if expr not in EXPR:
        expr = "normal"
    robot_get(f"/eyes?m={EXPR[expr]}", timeout=5)


def robot_move(cmd: str) -> None:
    if cmd not in VALID_MOVES:
        return
    timeout = 25 if cmd.startswith("dance") else 8
    robot_get(f"/move?cmd={cmd}", timeout=timeout)


def list_moves(timeout: int = 6) -> list[dict]:
    """GET /movelist — the ESP32's built-in expressive move library.

    Returns a list of {"n": index, "name": str} or [] on error.
    """
    try:
        r = robot_request("GET", "/movelist", timeout=timeout)
        r.raise_for_status()
        return r.json()
    except Exception as exc:
        _log(f"movelist failed: {exc}")
        return []


def play_move(n: int, timeout: int = 30) -> bool:
    """GET /playmove?n=N — play built-in library move N.

    The firmware replies 200 immediately, then plays the move as a blocking
    on-device sequence (and sets the move's baked-in face expression). Callers
    should wait roughly the move's duration before issuing the next command so
    playbacks don't overlap. Returns True if the trigger was accepted.
    """
    return robot_get(f"/playmove?n={int(n)}", timeout=timeout)


def robot_ticker(text: str) -> None:
    robot_get(f"/ticker?text={urllib.parse.quote(text[:64])}", timeout=5)


def show_screen(mode: str, msg: str = "") -> None:
    mode = mode.lower().strip()
    if mode == "text" and msg:
        robot_ticker(msg)
    elif mode in ("idle", "listen", "speak"):
        robot_get(f"/screen/{mode}", timeout=5)


def robot_state() -> dict:
    """GET /state — returns parsed JSON dict or empty dict on error."""
    try:
        r = robot_request("GET", "/state", timeout=6)
        r.raise_for_status()
        return r.json()
    except Exception as exc:
        _log(f"state poll failed: {exc}")
        return {}


def robot_status() -> dict:
    try:
        response = robot_request("GET", "/hw", timeout=6)
        return {"online": True, "host": ROBOT_HOST, "hardware": response.json()}
    except Exception as exc:
        return {"online": False, "host": ROBOT_HOST, "error": str(exc)}


def liftdetect_on(enabled: bool) -> None:
    """Enable or disable the ESP32 pick-up / shake lift detection."""
    if not IMU_ENABLED:
        return
    robot_get(f"/liftdetect?on={'1' if enabled else '0'}", timeout=5)
    _log(f"liftdetect → {'ON' if enabled else 'OFF'}")


def helpme_on(enabled: bool) -> None:
    """Enable or disable the ESP32 'help me' scared cry."""
    if not IMU_ENABLED:
        return
    robot_get(f"/helpme?on={'1' if enabled else '0'}", timeout=5)
    _log(f"helpme → {'ON' if enabled else 'OFF'}")


def robot_stand_and_idle() -> None:
    robot_move("stand")
    set_expression("normal")
    show_screen("idle")
    robot_ticker("Ready")


# ── Rock-Paper-Scissors game (Python-driven) ──────────────────────────────────
def rps_countdown() -> bool:
    """
    Play the 1-2-3 countdown on the robot and FREEZE on '3'.
    Blocks ~2.5 s (the countdown) then returns — snap the camera right after.
    """
    return robot_get("/rps/go", timeout=8)


def eyes_demo(stop: bool = False) -> bool:
    """Start (or stop) the firmware 'liquid eyes' animation: the eyes slide LEFT,
    then RIGHT, then SHAKE, then turn ANGRY over ~11 s. Returns immediately — the
    animation runs on-device. Pass stop=True to end it early (GET /eyes/demo?stop=1)."""
    return robot_get("/eyes/demo" + ("?stop=1" if stop else ""), timeout=5)


def rps_reveal(pick: str) -> str | None:
    """Show Bob's chosen throw and hold it on screen. Returns the pick echoed back."""
    if pick not in ("rock", "paper", "scissors"):
        return None
    try:
        r = robot_request("GET", "/rps/reveal", params={"pick": pick}, timeout=8)
        r.raise_for_status()
        return r.json().get("pick", pick)
    except Exception as exc:
        _log(f"rps_reveal failed: {exc}")
        return None


# ── Camera (BOB V2 — OV2640 built into ESP32-S3 main firmware) ───────────────
#
# BOB V2 merges the camera into the main firmware on port 80.
# Snapshot endpoint: GET http://<ROBOT_HOST>/cam/snapshot  → JPEG
# The old /control + /capture API (ESP32-CAM sketch) is gone.
# CAM_FRAMESIZE / CAM_QUALITY / CAM_FLASH env vars are no longer sent
# to the robot (the firmware uses its own fixed QVGA / quality-12 config).
# They are kept in .env.example for documentation only.

def cam_capture(timeout: int = 12) -> bytes | None:
    """Grab a single JPEG frame from the BOB V2 built-in camera.

    Calls GET /cam/snapshot on ROBOT_HOST (port 80).
    Returns JPEG bytes or None on failure.
    """
    url = ROBOT_BASE_URL + "/cam/snapshot"
    try:
        r = robot_request("GET", "/cam/snapshot", timeout=timeout)
        r.raise_for_status()
        return r.content   # success is silent — the vision thread grabs ~3×/s
    except Exception as exc:
        _log(f"cam capture failed ({url}): {exc}")
        return None
