#!/usr/bin/env python3
"""
BOB_ROBO_CONVO — Robot mode controller + conversation agent
=============================================================

Two modes, switchable via HTTP curl:

  IDLE MODE  (default at startup)
  ─────────────────────────────────────────────────────────
  • Robot sits with a normal face and "Ready" ticker
  • Small fidgets: every IDLE_ANIM_MIN_GAP..MAX_GAP seconds
      Bob plays a random SMALL move (nod, look around, curious,
      …) via /playmove, then settles. Big/showy moves are NOT
      used here — they're reserved for conversation mode.
      Disable with IDLE_ANIM_ENABLED=0.
  • Presence: polls the camera (MediaPipe face detection) and
      greets a person who walks up ("Hi, I am Bob!") with a
      wave, then waves goodbye when they leave. Disable with
      FACE_DETECT_ENABLED=0.
  • Shake / pick-up / scared reactions are ENABLED:
      robot is polled every 2 s; if the /state endpoint
      reports scaredMode=1 or reactState>0, the Mac plays
      a scared TTS reaction and sets the Angry expression
      once the reaction winds down
  • Wake word: Bob keeps listening (short mic windows →
      Whisper STT); hearing "hey bob" / "bob" flips him
      straight into CONVO mode. Disable with
      WAKE_WORD_ENABLED=0.
  • Loop runs in background thread — non-blocking

  CONVO MODE
  ─────────────────────────────────────────────────────────
  • Scared / shake reactions are SUSPENDED
  • VAD mic loop starts: captures speech → Whisper STT →
      Ollama qwen2.5:3b → set expression + ticker + TTS + move
  • Loop runs until user says goodbye or max_turns exceeded
  • On exit: automatically returns to IDLE mode

HTTP endpoints (port 8090):
  GET  /                     → live web dashboard (chat + MediaPipe camera feed)
  GET  /events?since=<id>    → chat log, status, mode, active mic, face count
  GET  /cam/frame.jpg        → latest MediaPipe-annotated camera frame (JPEG)
  POST /mode/idle            → switch to idle mode
  POST /mode/convo           → switch to convo mode
  GET  /mode                 → {"mode": "idle"|"convo"}
  GET  /health               → {"ok": true}

The dashboard is served by this Python process only — the robot firmware (.ino)
display/UI is NOT touched. Open http://<this-mac-ip>:8090/ in a browser to watch
Bob: the camera panel shows MediaPipe face detection live, and the conversation
streams in as chat messages the moment Bob hears his wake word.

All robot control is via direct HTTP to the ESP32 (ROBOT_HOST).
"""
from __future__ import annotations

import os
import re
import sys
import json
import subprocess
import textwrap
import unicodedata
import random
import threading
import time
import uuid
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

# Folder where ESP32-CAM snapshots taken during RPS rounds are saved
_CAPTURE_DIR = Path(__file__).parent / "screen_captures"
_CAPTURE_DIR.mkdir(exist_ok=True)

from dotenv import load_dotenv

load_dotenv(dotenv_path=Path(__file__).parent / ".env", override=True)
load_dotenv(dotenv_path=Path(__file__).parent.parent / "BOB_ASSISTANT" / ".env", override=False)

# ── Local modules ─────────────────────────────────────────────────────────────
sys.path.insert(0, str(Path(__file__).parent / "lib"))
import robo_vad     as _vad
import robo_llm     as _llm
import robo_robot   as _robot
import robo_tts     as _tts
import robo_gesture as _gesture
import robo_face    as _face

def _display_pages(text: str) -> list[str]:
    # The TFT font is ASCII; normalize punctuation instead of sending UTF-8
    # bytes which appear as garbage glyphs on its single-byte font.
    text = text.translate(str.maketrans({"’": "'", "‘": "'", "“": '"',
                                       "”": '"', "—": "-", "–": "-", "…": "..."}))
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    return textwrap.wrap(" ".join(text.split()), width=26,
                         break_long_words=True, break_on_hyphens=False)


def _speak_reply(text: str, expression: str = "normal", ticker: str = "") -> bool:
    """Show the full reply, with remaining pages after blocking audio playback.

    `ticker` is retained for caller compatibility; the LLM's shortened show_text
    must not replace the actual spoken reply on the robot.

    DISPLAY_DELAY_MS (from .env / OPTIMAL.env): if > 0, fires display updates
    after the delay so screen and audio are more synchronised.  0 = screen first
    (original behaviour — text visible while TTS is still rendering).
    """
    pages = _display_pages(text)
    with _robot.foreground_activity():
        def update_display():
            if DISPLAY_DELAY_MS > 0:
                _state.stop_event.wait(timeout=DISPLAY_DELAY_MS)
            _robot.set_expression(expression)
            _robot.show_screen("speak")
            if pages:
                _robot.robot_ticker(pages[0])
        stop = _state.stop_event
        started = time.monotonic()
        ok = _tts.robot_speak(text, play_locally=PLAY_LOCALLY,
                              before_audio=update_display)
        # Firmware cannot page its ticker inside synchronous I2S playback.
        # Send each remaining page explicitly; never truncate a long reply.
        if pages:
            stop.wait(max(0, 2.0 - (time.monotonic() - started)))
        for page in pages[1:]:
            if stop.is_set():
                break
            _robot.robot_ticker(page)
            stop.wait(2.0)
        return ok


# ── Config ────────────────────────────────────────────────────────────────────
PORT           = int(os.getenv("ROBO_CONVO_PORT",  "8090"))
PLAY_LOCALLY   = os.getenv("TTS_PLAY_LOCALLY", "0").strip() == "1"
# Optional delay (ms) inserted before firing display commands so the screen
# update arrives just as the speaker starts rather than 300-1500ms earlier.
# 0 = current "screen first" behaviour. Set from tests/results/OPTIMAL.env.
DISPLAY_DELAY_MS = float(os.getenv("DISPLAY_DELAY_MS", "0")) / 1000.0
IDLE_POLL_SECS = int(os.getenv("IDLE_POLL_SECS",   "2"))
MAX_TURNS      = int(os.getenv("CONVO_MAX_TURNS",  "20"))
# Idle "liveliness": in idle mode, play a random SMALL built-in move every so
# often so Bob feels alive. Gaps are random between MIN and MAX seconds (from the
# end of one move to the start of the next). Small fidgets are kept short/frequent.
# Big/showy moves (sneeze, celebrate, kung fu, …) are intentionally NOT used in
# idle — they are reserved for conversation mode driven by the LLM.
# Set IDLE_ANIM_ENABLED=0 to disable idle fidgets.
IDLE_ANIM_ENABLED = os.getenv("IDLE_ANIM_ENABLED", "1").strip() == "1"
IDLE_ANIM_MIN_GAP = float(os.getenv("IDLE_ANIM_MIN_GAP", "2"))
IDLE_ANIM_MAX_GAP = float(os.getenv("IDLE_ANIM_MAX_GAP", "6"))

# Camera presence detection: in idle mode, poll the built-in camera and greet a
# person who walks up ("Hi, I am Bob!") and wave goodbye when they leave. Uses
# MediaPipe face detection (lib/robo_face.py). Set FACE_DETECT_ENABLED=0 to skip.
FACE_DETECT_ENABLED = os.getenv("FACE_DETECT_ENABLED", "1").strip() == "1"
FACE_POLL_SECS      = float(os.getenv("FACE_POLL_SECS", "2"))
# Vision thread: single owner of the robot camera. Grabs frames as fast as
# CAM_GRAB_SECS allows (feeds the constant MJPEG stream in the UI) and runs
# MediaPipe every MP_DETECT_SECS (~2 Hz) — video stays smooth, boxes update ~2/s.
# The browser stream repeats the latest frame at CAM_STREAM_FPS so it never blanks.
# The browser now shows the robot's native :81 MJPEG stream (smooth). The server
# only grabs snapshots for MediaPipe face-count + the Hi/Bye greeting, so ~2 Hz
# is plenty and keeps port-80 load low.
CAM_GRAB_SECS   = float(os.getenv("CAM_GRAB_SECS",   "0.5"))
MP_DETECT_SECS  = float(os.getenv("MP_DETECT_SECS",  "0.5"))
CAM_STREAM_FPS  = float(os.getenv("CAM_STREAM_FPS",  "12"))
# Degrees to rotate the raw camera frame in the browser (camera is mounted 180°).
CAM_ROTATE_DEG  = int(os.getenv("CAM_ROTATE", "180"))
# How many consecutive detections before greeting, so a single false-positive
# frame from MediaPipe cannot trigger a hello.
FACE_PRESENT_TICKS  = int(os.getenv("FACE_PRESENT_TICKS", "3"))
# How many consecutive empty polls before we treat the person as gone (debounce
# so a single missed frame doesn't trigger a premature goodbye).
FACE_ABSENT_TICKS   = int(os.getenv("FACE_ABSENT_TICKS", "2"))
WHISPER_MODEL  = os.getenv("WHISPER_MODEL",        "distil-small.en")
_LISTEN_WAV    = "/tmp/robo_convo_input.wav"

# Wake word: in idle mode Anvyxo keeps listening (short mic windows → Whisper STT)
# and flips into conversation mode the moment he hears his name — "hey anvyxo" or
# just "anvyxo". Words are matched on word boundaries after lower-casing and
# stripping punctuation, so "Hey, Anvyxo!" and "anvyxo?" both trigger. The old
# "bob" variants are kept as a fallback because Whisper often mis-transcribes the
# coined name "anvyxo". Set WAKE_WORD_ENABLED=0 to keep idle mode silent/passive.
WAKE_WORD_ENABLED = os.getenv("WAKE_WORD_ENABLED", "1").strip() == "1"
WAKE_WORDS        = tuple(
    w.strip().lower()
    for w in os.getenv(
        "WAKE_WORDS",
        "hey anvyxo,anvyxo,hi anvyxo,ok anvyxo,hey bob,bob,hi bob,ok bob",
    ).split(",")
    if w.strip()
)
# Length (seconds) of each wake-word listen window. Robot mic caps this at 3 s.
WAKE_LISTEN_SECS  = float(os.getenv("WAKE_LISTEN_SECS", "3"))
# Quiet gap between wake-word listens. Without this, idle records the robot mic
# back-to-back, which hammers the S3 (starving fidgets/camera and making the
# robot look permanently "listening"). A short gap keeps Bob responsive to his
# name while leaving the robot room to breathe.
WAKE_GAP_SECS     = float(os.getenv("WAKE_GAP_SECS", "1.5"))
_WAKE_WAV         = "/tmp/robo_wake_input.wav"

# Which mic each mode uses. Wake-word listening runs on the robot's own mic
# (SPH0645) so Bob hears his name across the room; conversation runs on the Mac
# mic so it gets fast VAD (stops the instant you finish speaking) for a snappy
# back-and-forth. Override per deployment if needed.
WAKE_MIC_SOURCE  = os.getenv("WAKE_MIC_SOURCE",  "robot").strip().lower()
CONVO_MIC_SOURCE = os.getenv("CONVO_MIC_SOURCE", "mac").strip().lower()
# Only listen for the wake word when the camera actually sees someone. Keeps
# idle calm (no constant robot-mic recording / "listening" when nobody's there)
# and spares the ESP32. Ignored if face detection is unavailable. Set 0 to
# always listen.
WAKE_REQUIRE_FACE = os.getenv("WAKE_REQUIRE_FACE", "1").strip() == "1"

# ── Expressive moves ──────────────────────────────────────────────────────────
# The LLM picks a move by NAME (see robo_llm); we map the name to its /playmove
# index here. Dance moves are included so Bob CAN dance when asked, but the LLM
# is told to use them only on explicit dance requests. Filled from /movelist at
# startup by _init_moves(). The big move Bob plays when handing off to the Db2
# expert agent is DB2_MOVE.
_MOVE_IDX: dict[str, int] = {}
_CONVO_MOVES = [
    "Happy wiggle", "Excited hop", "Celebrate", "Bounce (sync)", "Wiggle",
    "Leg wave", "Double take", "Curious", "Curious lean", "Peek", "Sad droop",
    "Shiver", "Robot", "Dizzy", "Kung fu", "Waddle", "One leg stand",
    "Cha-cha", "Moonwalk slide", "Foot dance",
]
DB2_MOVE = os.getenv("DB2_MOVE", "Robot")
# Move Bob plays when picked up / shaken (a scared wobble).
SCARED_MOVE = os.getenv("SCARED_MOVE", "Shiver")


def _init_moves() -> None:
    """Load the robot's move library and register the expressive names with the LLM."""
    global _MOVE_IDX
    _MOVE_IDX = {m.get("name"): int(m["n"]) for m in _robot.list_moves() if m.get("name")}
    offered = [n for n in _CONVO_MOVES if n in _MOVE_IDX]
    _llm.set_available_moves(offered)
    _log("main", f"expressive moves: {len(offered)} offered to LLM, "
                 f"{len(_MOVE_IDX)} in /playmove map")


def _play_named_move(name: str) -> None:
    """Play a named /movelist move; fall back to legacy /move for fwd/bwd/etc."""
    idx = _MOVE_IDX.get(name)
    if idx is not None:
        _robot.play_move(idx)
    else:
        _robot.robot_move(name)


# Convo-mode gestures: can be disabled with CONVO_MOVE_ENABLED=0 so the robot
# stays still while thinking and doesn't monopolize the HTTP connection.
CONVO_MOVE_ENABLED = os.getenv("CONVO_MOVE_ENABLED", "0").strip() == "1"

_BIG_MOVES = [
    "Happy wiggle", "Excited hop", "Celebrate", "Bounce (sync)", "Wiggle",
    "Leg wave", "Double take", "Robot", "Dizzy", "Kung fu", "Waddle",
    "One leg stand", "Cha-cha", "Moonwalk slide", "Foot dance",
]

def _play_random_big_move() -> None:
    """Play one random big move if enabled."""
    if not CONVO_MOVE_ENABLED:
        return
    names = [n for n in _BIG_MOVES if n in _MOVE_IDX]
    if names:
        _play_named_move(random.choice(names))


def _take_photo() -> bool:
    """Snap a picture from Bob's camera and save it to the laptop (screen_captures).
    Bob has already said 'smile please'; this captures, saves, and confirms."""
    _dash.set_status("📸 Taking a photo…")
    frame = _robot.cam_capture()
    if not frame:
        _tts.robot_speak("Oops, my camera didn't work. Let's try again!",
                         play_locally=PLAY_LOCALLY)
        return False
    # Correct the upside-down camera mount if Pillow is available.
    rot = int(os.getenv("CAM_ROTATE", "180"))
    if rot:
        try:
            import io
            from PIL import Image
            img = Image.open(io.BytesIO(frame)).rotate(rot, expand=True)
            buf = io.BytesIO()
            img.save(buf, format="JPEG", quality=90)
            frame = buf.getvalue()
        except Exception:
            pass   # save the raw frame if Pillow isn't available
    path = _CAPTURE_DIR / f"photo_{time.strftime('%Y%m%d_%H%M%S')}.jpg"
    path.write_bytes(frame)
    _log("convo", f"photo saved → screen_captures/{path.name} ({len(frame)} bytes)")
    _dash.add_message("system", f"📸 Photo saved: {path.name}")
    _robot.set_expression("happy")
    _robot.robot_ticker("Got it! 📸")
    _tts.robot_speak("Got it! Great photo!", play_locally=PLAY_LOCALLY)
    return True


# ── Logging ───────────────────────────────────────────────────────────────────
def _log(tag: str, msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] [{tag}] {msg}", file=sys.stderr, flush=True)


# ── Whisper — loaded ONCE at startup so first turn has no download delay ─────
_whisper = None

def _load_whisper():
    global _whisper
    from faster_whisper import WhisperModel
    _log("whisper", f"loading {WHISPER_MODEL} (this may download ~200 MB first time) …")
    _whisper = WhisperModel(WHISPER_MODEL, device="cpu", compute_type="int8")
    _log("whisper", "ready ✓")

def _transcribe(wav: str) -> str:
    wav_kb = Path(wav).stat().st_size // 1024 if Path(wav).exists() else 0
    _log("whisper", f"transcribing {wav_kb} KB …")
    t0 = time.time()
    segs, info = _whisper.transcribe(
        wav, language="en",
        vad_filter=True,
        vad_parameters={"min_silence_duration_ms": 300},
    )
    result = " ".join(s.text.strip() for s in segs).strip()
    _log("whisper", f"✓ {time.time()-t0:.2f}s  dur={info.duration:.1f}s  \"{result}\"")
    return result


def _heard_wake_word(stop: threading.Event) -> bool:
    """Record one short window and return True if a wake word was spoken.

    Used by the idle loop to listen for "hey bob" / "bob". Reuses the same mic
    (robot SPH0645 or Mac) and Whisper pipeline as conversation mode. On a
    failed capture (mic/robot unavailable) it waits one poll interval so the
    idle loop doesn't spin.
    """
    _dash.set_mic_active(True)
    got = _vad.capture_until_silence(out_path=_WAKE_WAV,
                                     max_duration_s=int(WAKE_LISTEN_SECS),
                                     source=WAKE_MIC_SOURCE)
    _dash.set_mic_active(False)
    if not got:
        stop.wait(timeout=IDLE_POLL_SECS)
        return False
    if stop.is_set():
        return False
    text = _transcribe(_WAKE_WAV).strip().lower()
    if not text:
        return False
    norm = re.sub(r"[^a-z0-9 ]", " ", text)
    if any(re.search(rf"\b{re.escape(w)}\b", norm) for w in WAKE_WORDS):
        _log("idle", f"wake word matched in: \"{text}\"")
        return True
    return False


# ─────────────────────────────────────────────────────────────────────────────
#  MODE STATE
# ─────────────────────────────────────────────────────────────────────────────
class ModeState:
    IDLE  = "idle"
    CONVO = "convo"
    DEMO  = "demo"    # scripted "ANVYXO // ONLINE" intro skit (no mic)

    def __init__(self):
        self._mode    = self.IDLE
        self._lock    = threading.Lock()
        self._stop_ev = threading.Event()   # signals background threads to stop

    @property
    def mode(self) -> str:
        with self._lock:
            return self._mode

    def set_mode(self, new_mode: str):
        with self._lock:
            if self._mode == new_mode:
                return False
            _log("mode", f"{self._mode} → {new_mode}")
            self._stop_ev.set()          # tell running thread to stop
            self._mode = new_mode
            self._stop_ev = threading.Event()   # fresh event for next thread
            return True

    @property
    def stop_event(self) -> threading.Event:
        with self._lock:
            return self._stop_ev


_state = ModeState()


# ─────────────────────────────────────────────────────────────────────────────
#  WEB DASHBOARD STATE  (served by server.py — the robot firmware UI is untouched)
# ─────────────────────────────────────────────────────────────────────────────
class _Dashboard:
    """Thread-safe snapshot of what the browser dashboard shows.

    Holds the latest MediaPipe-annotated camera frame + face count, a short
    status line, and a rolling chat log. The idle/convo loops push updates here;
    the HTTP handler reads them for /events and /cam/frame.jpg. This is a
    read-only mirror of what Bob is doing — it never drives the robot.
    """
    _MAX_MSGS = 120

    def __init__(self):
        self._lock     = threading.Lock()
        self._frame: bytes | None = None
        self._faces    = 0
        self._frame_ts = 0.0
        self._status   = "Starting up…"
        self._mic_active = False        # True while the mic is actively recording
        self._messages: list[dict] = []
        self._seq      = 0

    def set_mic_active(self, on: bool) -> None:
        with self._lock:
            self._mic_active = on

    def set_frame(self, jpeg: bytes | None, faces: int | None = None) -> None:
        """Store the latest camera frame. faces=None keeps the previous count
        (used for raw frames between the ~2 Hz MediaPipe detections)."""
        with self._lock:
            if jpeg:
                self._frame = jpeg
                self._frame_ts = time.time()
            if faces is not None:
                self._faces = faces

    def frame(self) -> bytes | None:
        with self._lock:
            return self._frame

    def face_count(self) -> int:
        with self._lock:
            return self._faces

    def set_faces(self, n: int) -> None:
        with self._lock:
            self._faces = n

    def set_status(self, text: str) -> None:
        with self._lock:
            self._status = text

    def add_message(self, role: str, text: str) -> None:
        """role ∈ {user, bob, db2, system}. Blank text is ignored."""
        text = (text or "").strip()
        if not text:
            return
        with self._lock:
            self._seq += 1
            self._messages.append(
                {"id": self._seq, "role": role, "text": text, "ts": time.time()}
            )
            if len(self._messages) > self._MAX_MSGS:
                self._messages = self._messages[-self._MAX_MSGS:]

    def events(self, since: int = 0) -> dict:
        with self._lock:
            return {
                "faces":    self._faces,
                "frame_ts": self._frame_ts,
                "has_frame": self._frame is not None,
                "status":   self._status,
                "mic_active": self._mic_active,
                "seq":      self._seq,
                "messages": [m for m in self._messages if m["id"] > since],
            }


_dash = _Dashboard()


def _active_mic() -> str:
    """Which mic is in use right now: Mac for conversation, robot for wake-word."""
    return CONVO_MIC_SOURCE if _state.mode == ModeState.CONVO else WAKE_MIC_SOURCE


# ─────────────────────────────────────────────────────────────────────────────
#  IDLE MODE — scared / shake watcher
# ─────────────────────────────────────────────────────────────────────────────
# Scared reaction phrases — played once when the robot is picked up / shaken.
# The robot already shows the scared face on its own display; Mac TTS gives it
# a voice so it feels alive.
# Each entry is (spoken line, screen ticker, face expression). The ticker is
# short so it reads on the tiny robot screen; the expression is chosen to match
# the mood of the line (surprised panic vs. mock-angry grumbling). Tickers are
# plain ASCII — the robot's screen renders emoji as garbage glyphs.
_SCARED_REACTIONS = [
    ("STOP SHAKING ME!",                                    "STOP SHAKING ME!",  "angry"),
    ("Whoa whoa whoa! I am a robot, not a maraca!",         "Not a maraca!",     "surprised"),
    ("Put me down! I get terrible motion sickness!",        "Motion sickness!",  "surprised"),
    ("Hey! I did not sign up for this rollercoaster!",      "Rollercoaster?!",   "angry"),
    ("Error four oh four: dignity not found. Put me down!", "404: dignity",      "angry"),
    ("Unhand me, giant! I bruise like a banana!",           "I bruise easy!",    "surprised"),
]
_scared_idx = 0

# Moves Bob throws when shaken — an indignant karate combo (vigorous but quick).
SHAKE_MOVE  = os.getenv("SHAKE_MOVE",  "Kung fu")
SHAKE_MOVE2 = os.getenv("SHAKE_MOVE2", "Tantrum")


def _play_move_combo(*names: str, beat: float = 2.4) -> None:
    """Play several named library moves back-to-back, waiting out each one so the
    firmware doesn't drop an overlapping command. Skips names it can't resolve."""
    for name in names:
        if not name:
            continue
        _play_named_move(name)
        _state.stop_event.wait(beat)


def _scared_reaction() -> None:
    """Full pick-up/shake reaction: angry face + a spoken line + a karate combo.
    Used by the idle shake-watcher and by the /test/scare button.

    Order matters: the firmware fires its own scared foot-flip the instant it
    detects the shake, so we SPEAK first (that flip + the chassis ringing are
    done by the time the voice finishes uploading) and only THEN throw the Mac
    moves onto a quiet servo bus."""
    global _scared_idx
    phrase, ticker, expr = _SCARED_REACTIONS[_scared_idx % len(_SCARED_REACTIONS)]
    _scared_idx += 1
    _log("idle", f"scared! → \"{phrase}\"")
    _dash.add_message("system", "😱 Picked up / shaken!")
    _dash.add_message("bob", phrase)
    _robot.set_expression(expr)
    _robot.show_screen("speak")
    _robot.robot_ticker(ticker)
    _tts.robot_speak(phrase, play_locally=PLAY_LOCALLY)
    _play_move_combo(SHAKE_MOVE, SHAKE_MOVE2)   # vigorous karate combo, bus quiet


# ── Tilt reactions — Bob grumbles when you lean him left / right ───────────────
# Fired by the idle loop from the IMU `roll` angle (see /state). Each is
# (spoken line, screen ticker, face, move). Tickers are plain ASCII (robot
# screen); the move is a quick, animated body reaction to the lean.
# TILT_DEG = how far to lean before it trips; re-centre below ~40% of it to rearm.
TILT_DEG = float(os.getenv("TILT_DEG", "22"))
_TILT_REACTIONS = {
    "left":  ("Woah—",  "<- NOT HAPPY",  "surprised", "Double take"),
    "right": ("Bro.",   "NOT HAPPY ->",  "angry",     "Wiggle"),
}


def _tilt_reaction(side: str) -> None:
    """React once to a sustained left/right tilt: a grumble + screen text + move."""
    phrase, ticker, expr, move = _TILT_REACTIONS[side]
    _log("idle", f"tilt {side} → \"{phrase}\"")
    _dash.add_message("system", f"↔ Tilted {side}")
    _dash.add_message("bob", phrase)
    _robot.set_expression(expr)
    _robot.show_screen("speak")
    _robot.robot_ticker(ticker)
    _tts.robot_speak(phrase, play_locally=PLAY_LOCALLY)
    _play_named_move(move)   # animated lean reaction


# ─────────────────────────────────────────────────────────────────────────────
#  IDLE ANIMATION LIBRARY
# ─────────────────────────────────────────────────────────────────────────────
# Idle mode plays ONLY small, gentle fidgets — kept short and frequent so Bob
# reads as quietly alive. Big / showy / locomotion / aggressive moves (Happy
# wiggle, Celebrate, Sneeze, Robot, Dizzy, Cha-cha, Kung fu, Tantrum, Kick, Walk
# forward, Turn left/right, …) are deliberately NOT used in idle; they are
# reserved for conversation mode, where the LLM chooses them. Keys match the
# /movelist names exactly; moves the firmware doesn't expose are simply skipped.
_IDLE_MOVE_WEIGHTS: dict[str, int] = {
    "Little nod":   6,
    "Look around":  6,
    "Curious":      5,
    "Curious lean": 5,
    "Sleepy rock":  5,
    "Double take":  3,
    "Hiccup":       2,
}

# Greeting / farewell gesture: a friendly wave when a person walks up or leaves.
# Falls back to a nod if the firmware doesn't expose "Leg wave".
_GREET_MOVE_NAMES = ["Leg wave", "Little nod"]

# Approximate playback duration (seconds) per move — used to wait out the
# blocking on-device playback before scheduling the next thing. The firmware
# replies 200 before it finishes playing, so we sleep this long to avoid
# overlapping moves. Default is used for anything not listed.
_MOVE_SECS_DEFAULT = 2.5
_MOVE_SECS: dict[str, float] = {
    "Little nod":   1.5,
    "Look around":  2.0,
    "Curious":      2.0,
    "Curious lean": 1.8,
    "Sleepy rock":  1.8,
    "Double take":  1.8,
    "Hiccup":       1.5,
    "Leg wave":     3.0,
}

# Light "alive" ticker phrases occasionally shown alongside an idle fidget.
_IDLE_TICKERS = ["Hmm...", "La la la", "Just chilling", "Anvyxo time",
                 "Doo dee doo", "\U0001f916", "Beep boop", "Waiting..."]

# Spoken when the camera first sees someone / when they leave.
_HELLO_PHRASES = [
    "Oh, hello there! I am Anvyxo!",
    "Hi! I am Anvyxo, nice to see you!",
    "Hello! Anvyxo here. How are you?",
]
_HELLO_PHRASES_MANY = [
    "Hello everyone! I am Anvyxo!",
    "Hi friends! I am Anvyxo, welcome!",
]
_BYE_PHRASES = [
    "Goodbye! See you later!",
    "Bye bye! Come back soon!",
    "See you around!",
]


def _build_idle_menu() -> list[tuple[int, str, int, float]]:
    """Resolve the curated small-fidget names against the robot's /movelist.

    Returns a list of (index, name, weight, duration_secs) for every curated
    move the firmware actually exposes. Empty if the robot is unreachable or
    idle animation is disabled — the idle loop then just does the scared watch.
    """
    if not IDLE_ANIM_ENABLED:
        return []
    menu: list[tuple[int, str, int, float]] = []
    for m in _robot.list_moves():
        name = m.get("name", "")
        weight = _IDLE_MOVE_WEIGHTS.get(name)
        if weight:
            secs = _MOVE_SECS.get(name, _MOVE_SECS_DEFAULT)
            menu.append((int(m["n"]), name, weight, secs))
    return menu


def _pick_idle_move(menu: list[tuple[int, str, int, float]]):
    """Weighted-random pick from the idle menu → (index, name, duration)."""
    idx, name, _w, secs = random.choices(menu, weights=[e[2] for e in menu])[0]
    return idx, name, secs


def _resolve_greet_move() -> tuple[int, float] | None:
    """Find the greeting/farewell wave in the robot's move library."""
    names = {m.get("name"): int(m["n"]) for m in _robot.list_moves()}
    for name in _GREET_MOVE_NAMES:
        if name in names:
            return names[name], _MOVE_SECS.get(name, _MOVE_SECS_DEFAULT)
    return None


def _settle_idle():
    """Return the robot to the calm ready state between events."""
    _robot.set_expression("normal")
    _robot.show_screen("idle")
    _robot.robot_ticker("Ready")


def _greet_person(many: bool, greet: tuple[int, float] | None, stop: threading.Event):
    """Say hello and wave when someone walks up to Bob."""
    phrase = random.choice(_HELLO_PHRASES_MANY if many else _HELLO_PHRASES)
    _log("idle", f"person arrived ({'many' if many else 'one'}) → \"{phrase}\"")
    _dash.add_message("system", "👀 Someone walked up")
    _dash.add_message("bob", phrase)
    _robot.set_expression("happy")
    _robot.show_screen("speak")
    _robot.robot_ticker("Hi there!")
    if greet:
        _robot.play_move(greet[0])
        stop.wait(timeout=greet[1])
    _tts.robot_speak(phrase, play_locally=PLAY_LOCALLY)


def _farewell_person(greet: tuple[int, float] | None, stop: threading.Event):
    """Wave goodbye when the person leaves Bob's view."""
    phrase = random.choice(_BYE_PHRASES)
    _log("idle", f"person left → \"{phrase}\"")
    _dash.add_message("system", "🚶 Person left")
    _dash.add_message("bob", phrase)
    _robot.set_expression("love")
    _robot.show_screen("speak")
    _robot.robot_ticker("Bye!")
    if greet:
        _robot.play_move(greet[0])
        stop.wait(timeout=greet[1])
    _tts.robot_speak(phrase, play_locally=PLAY_LOCALLY)


def _idle_loop(stop: threading.Event):
    """
    Background thread for IDLE mode.

    Three interleaved behaviours, all in one poll loop:
      • Small fidgets — a gentle random move every few seconds so Bob looks
        alive (big/showy moves are reserved for conversation mode).
      • Presence — polls the camera; greets a person who walks up ("Hi, I am
        Bob!") with a wave and says goodbye when they leave.
      • Scared watch — if the IMU reports pick-up/shake, plays a scared phrase
        (unchanged from before).
    """
    global _scared_idx

    # Initialization uses the same ownership guard as idle work.
    with _robot.foreground_activity():
        if stop.is_set():
            return
        _log("idle", "▶ idle loop started")
        _settle_idle()
        _dash.set_status("Idle — say “hey Bob” to start a conversation")

        # Arm the ESP32 pick-up / shake detection AND the scared "help me" cry.
        # This is essential, not optional: convo mode disables both (and the toggle
        # persists to the ESP32's NVS), so an interrupted convo — or a robot reboot —
        # can leave them off. Enabling /liftdetect also runs a fresh IMU calibration,
        # which lift detection requires after every power-on. Without this, the
        # scared watcher below polls forever but scaredMode/react never trip.
        imu = _robot.IMU_ENABLED
        if imu:
            _robot.liftdetect_on(True)   # arms + auto-calibrates (robot must be still)
            _robot.helpme_on(True)
            _log("idle", "lift detection + scared cry armed (IMU calibrated)")

        # Small-fidget menu, resolved from the robot's actual move library.
        menu = _build_idle_menu()
        if menu:
            _log("idle", f"idle fidgets ON — {len(menu)} small moves, "
                         f"gap {IDLE_ANIM_MIN_GAP:.0f}-{IDLE_ANIM_MAX_GAP:.0f}s")
            next_anim_at = time.time() + random.uniform(IDLE_ANIM_MIN_GAP, IDLE_ANIM_MAX_GAP)
        else:
            if IDLE_ANIM_ENABLED:
                _log("idle", "idle fidgets OFF — no moves resolved from /movelist")
            next_anim_at = None

        # Camera presence detection.
        face_on = FACE_DETECT_ENABLED and _face.warm_up()
        greet   = _resolve_greet_move() if face_on else None
        if face_on:
            _log("idle", f"presence detection ON — polling camera every {FACE_POLL_SECS:.0f}s")
        elif FACE_DETECT_ENABLED:
            _log("idle", "presence detection OFF — face worker unavailable (run ./setup_gesture.sh)")
        person_present = False
        absent_ticks   = 0
        present_ticks  = 0
        next_face_at   = time.time() + FACE_POLL_SECS

        if not imu:
            _log("idle", "IMU reactions disabled (ROBOT_IMU_ENABLED=0); shake polling skipped")
        was_scared = False
        tilt_side  = None   # None=level, "left"/"right"=already reacted to this lean

        # Wake-word listening: keep an ear out for "hey bob" / "bob" and jump into
        # conversation mode when heard. Needs the Whisper model that main() pre-loads.
        wake_on = WAKE_WORD_ENABLED and _whisper is not None
        if wake_on:
            gate = "when a face is seen" if WAKE_REQUIRE_FACE else "always"
            _log("idle", f"wake word ON ({WAKE_MIC_SOURCE} mic, listens {gate}) — say "
                         f"one of {list(WAKE_WORDS)} to start a chat "
                         f"({WAKE_LISTEN_SECS:.0f}s windows)")
        elif WAKE_WORD_ENABLED:
            _log("idle", "wake word OFF — Whisper model not loaded")

    while not stop.is_set():
        with _robot.idle_activity() as acquired:
            if not acquired:
                stop.wait(0.1)
                continue
            # ── Scared / shake watcher (only if the IMU is enabled) ────────────────
            scared_now = False
            if imu:
                try:
                    status = _robot.robot_state()   # GET /state → parsed JSON dict
                except Exception:
                    stop.wait(timeout=IDLE_POLL_SECS)
                    continue
                scared_now = status.get("scaredMode", 0) or status.get("react", 0)

                if scared_now and not was_scared:
                    # Rising edge — robot just got picked up / shaken
                    _scared_reaction()
                    was_scared = True
                    tilt_side  = None   # a shake re-arms the tilt grumble
                    # Don't animate while being handled; push the next fidget out.
                    if next_anim_at is not None:
                        next_anim_at = time.time() + random.uniform(IDLE_ANIM_MIN_GAP, IDLE_ANIM_MAX_GAP)

                elif not scared_now and was_scared:
                    # Falling edge — reaction finished; return to normal
                    _log("idle", "reaction ended → normal")
                    _settle_idle()
                    was_scared = False

                # ── Tilt watch — grumble once per lean, only while not being shaken ──
                # Uses the IMU roll angle now included in /state. Fires when the
                # lean passes TILT_DEG and re-arms once Bob is brought back near
                # level, so each distinct tilt gets exactly one reaction.
                elif not scared_now:
                    try:
                        roll = float(status.get("roll", 0) or 0)
                    except (TypeError, ValueError):
                        roll = 0.0
                    if roll <= -TILT_DEG and tilt_side != "left":
                        _tilt_reaction("left");  tilt_side = "left"
                        _settle_idle()
                    elif roll >= TILT_DEG and tilt_side != "right":
                        _tilt_reaction("right"); tilt_side = "right"
                        _settle_idle()
                    elif abs(roll) < TILT_DEG * 0.4:
                        tilt_side = None   # back near level → re-arm

            if _robot.FOREGROUND_BUSY.is_set():
                continue

            # ── Camera presence — greet arrivals, wave off departures ──────────────
            if face_on and not scared_now and not was_scared and time.time() >= next_face_at:
                faces = _dash.face_count()      # from the vision thread (single camera owner)
                next_face_at = time.time() + FACE_POLL_SECS

                if faces > 0:
                    absent_ticks = 0
                    present_ticks += 1
                    # Require several CONSECUTIVE detections before greeting, so a
                    # single false-positive frame from MediaPipe can't trigger a hello.
                    if not person_present and present_ticks >= FACE_PRESENT_TICKS:
                        person_present = True
                        _greet_person(faces > 1, greet, stop)
                        if stop.is_set():
                            break
                        _settle_idle()
                        # Hold off fidgets briefly so the greeting can breathe.
                        if next_anim_at is not None:
                            next_anim_at = time.time() + random.uniform(IDLE_ANIM_MIN_GAP, IDLE_ANIM_MAX_GAP)
                elif person_present:
                    present_ticks = 0
                    absent_ticks += 1
                    if absent_ticks >= FACE_ABSENT_TICKS:
                        person_present = False
                        absent_ticks = 0
                        _farewell_person(greet, stop)
                        if stop.is_set():
                            break
                        _settle_idle()
                        if next_anim_at is not None:
                            next_anim_at = time.time() + random.uniform(IDLE_ANIM_MIN_GAP, IDLE_ANIM_MAX_GAP)

            if _robot.FOREGROUND_BUSY.is_set():
                continue

            # ── Small idle fidget — a COMBO of 3 small moves so it reads as alive ───
            if (next_anim_at is not None and not scared_now and not was_scared
                    and time.time() >= next_anim_at):
                combo = [_pick_idle_move(menu) for _ in range(3)]   # different combo each time
                _log("idle", "fidget combo → " + ", ".join(n for _, n, _ in combo))
                if random.random() < 0.4:
                    _robot.robot_ticker(random.choice(_IDLE_TICKERS))
                for idx, name, secs in combo:
                    if _robot.FOREGROUND_BUSY.is_set():
                        break
                    _robot.play_move(idx)       # firmware also sets the move's face
                    stop.wait(timeout=secs)     # let each blocking playback finish
                    if stop.is_set():
                        break
                if stop.is_set():
                    break
                if not _robot.FOREGROUND_BUSY.is_set():
                    _settle_idle()
                next_anim_at = time.time() + random.uniform(IDLE_ANIM_MIN_GAP, IDLE_ANIM_MAX_GAP)

            if _robot.FOREGROUND_BUSY.is_set():
                continue

            # ── Wake-word listen — "hey bob" / "bob" opens a conversation ──────────
            # The recording itself paces the loop (~a few seconds), so we skip the
            # extra poll sleep and listen again promptly. Suspended while the robot
            # is being handled (scared) so we don't record the reaction. When
            # WAKE_REQUIRE_FACE is on and the camera is available, only listen while
            # someone is actually present — keeps idle calm and off the robot mic.
            wake_gated = WAKE_REQUIRE_FACE and face_on and not person_present
            if wake_on and not wake_gated and not scared_now and not was_scared and not stop.is_set():
                if _heard_wake_word(stop):
                    _log("idle", "wake word heard → switching to convo mode")
                    _dash.add_message("system", "🔔 Heard “hey Bob” — starting conversation")
                    _dash.set_status("Waking up…")
                    if _state.set_mode(ModeState.CONVO):
                        _launch_convo()
                    break
                # Brief gap so we're not recording the robot mic back-to-back.
                stop.wait(timeout=WAKE_GAP_SECS)
                continue

        stop.wait(timeout=IDLE_POLL_SECS)

    _log("idle", "✓ idle loop stopped")


# ─────────────────────────────────────────────────────────────────────────────
#  TICKER KEEP-ALIVE — re-sends a ticker message every 2 s until stopped
# ─────────────────────────────────────────────────────────────────────────────
class _TickerKeepAlive:
    """
    Context manager that keeps a ticker message visible on the robot display
    by re-sending it in a background thread every 2 seconds.

    Usage:
        with _TickerKeepAlive("Listening..."):
            _vad.capture_until_silence(...)
    """
    _INTERVAL = 2.0   # seconds between re-sends

    def __init__(self, text: str):
        self._text = text
        self._done = threading.Event()
        self._thread: threading.Thread | None = None

    def _run(self):
        while not self._done.wait(timeout=self._INTERVAL):
            _robot.robot_ticker(self._text)

    def __enter__(self):
        self._done.clear()
        _robot.robot_ticker(self._text)          # send immediately
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        return self

    def __exit__(self, *_):
        self._done.set()
        if self._thread:
            self._thread.join(timeout=self._INTERVAL + 1)


# ─────────────────────────────────────────────────────────────────────────────
#  ROCK-PAPER-SCISSORS — scripted skit (no camera, Bob always wins & sulks)
# ─────────────────────────────────────────────────────────────────────────────
# A canned performance, not a real match. Bob trash-talks, throws paper, gloats,
# then throws a tantrum when the sore-loser human shakes him. The "YOU" lines are
# shown in the dashboard chat (never recorded) so the whole exchange reads like a
# real back-and-forth. Needs no camera or hand recognition — just TTS, the robot
# screen and a couple of library moves. Triggered from the dashboard button or by
# saying / typing "let's play a game".
#
# Moves used (resolved from /movelist via _play_named_move):
#   WIN    — a happy celebration after Bob wins ("Too easy, you are a noob")
#   SHIVER — a scared shiver on the shake, played SHIVER_TIMES times
#   CRY    — a crying motion after "someone help me". NB: "Foot flap" reads HAPPY
#            on this robot, so the cry uses "Sad droop" instead.
_RPS_WIN_MOVE     = os.getenv("RPS_WIN_MOVE",     "Happy wiggle")
_RPS_SHIVER_MOVE  = os.getenv("RPS_SHIVER_MOVE",  "Shiver")
_RPS_SHIVER_TIMES = int(os.getenv("RPS_SHIVER_TIMES", "3"))
_RPS_CRY_MOVE     = os.getenv("RPS_CRY_MOVE",     "Sad droop")


@_robot.foreground_activity()
def _play_rps_demo(stop: "threading.Event | None" = None) -> None:
    try:
        if stop is None or not stop.is_set():
            _run_rps_demo(stop)
    finally:
        # Release full-screen reveal even if stopped or synthesis fails.
        _robot.set_expression("normal")
        _robot.show_screen("idle")


def _run_rps_demo(stop: "threading.Event | None" = None) -> None:
    """The scripted Rock-Paper-Scissors skit. Bob wins with paper, then sulks.

    Pure TTS + screen theatre + two library moves — no camera. The "YOU" lines
    are dropped into the dashboard chat so it reads as a real exchange.
    """
    def _stopped() -> bool:
        return stop is not None and stop.is_set()

    def _beat(secs: float) -> None:
        stop.wait(secs) if stop is not None else time.sleep(secs)

    def _you(line: str, beat: float = 1.3) -> None:
        """Show a scripted human line in the chat (never spoken / recorded)."""
        _dash.add_message("user", line)
        _beat(beat)

    _log("game", "RPS skit — Bob wins, then throws a tantrum")
    _dash.add_message("system", "🎮 Rock · Paper · Scissors")

    # NB: the robot's little screen can't render emoji — they come out as garbage
    # glyphs — so ticker text (shown ON the robot) is plain ASCII. Emoji live only
    # in the dashboard chat, which is a browser.

    # ── Trash talk ────────────────────────────────────────────────────────────
    _you("Wanna play Rock Paper Scissors?")
    if _stopped():
        return
    # "surprised" (cocky raised brow) on purpose: switching to "happy" at the win
    # below is a real face CHANGE, which makes the firmware fully wipe the paper
    # reveal art off the screen.
    _dash.add_message("bob", "You're going to lose.")
    _speak_reply("You're going to lose.", "surprised")
    if _stopped():
        return
    _you("That's crazy confidence.")
    if _stopped():
        return

    # ── Countdown — spoken ONE BY ONE, screen + audio in sync ─────────────────
    # Each word shows on the robot screen and is said aloud before the next, so
    # it lands like a real "Rock... Paper... Scissors... SHOOT!" call.
    for word in ("Rock", "Paper", "Scissors"):
        _robot.set_expression("surprised")
        _robot.show_screen("speak")
        _robot.robot_ticker(word)
        _tts.robot_speak(word + "...", play_locally=PLAY_LOCALLY)
        if _stopped():
            return
    _robot.robot_ticker("SHOOT!")
    _tts.robot_speak("Shoot!", play_locally=PLAY_LOCALLY)
    if _stopped():
        return

    # ── Reveal PAPER → "ANVYXO WINS" + happy gloat ────────────────────────────
    _robot.rps_reveal("paper")          # [Robot wins]
    _robot.robot_ticker("ANVYXO WINS")
    _beat(1.3)
    _dash.add_message("system", "🏆 ANVYXO WINS")
    # "happy" here is a real face CHANGE from the "surprised" above, which makes
    # the firmware fully wipe the paper reveal art off the screen.
    _dash.add_message("bob", "Too easy. You are a noob.")
    _speak_reply("Too easy. You are a noob.", "happy")
    _play_named_move(_RPS_WIN_MOVE)     # happy victory wiggle
    _beat(2.0)
    if _stopped():
        return

    # ── Grabbed + shaken → "What about now?" → shiver 3× + cry ────────────────
    _dash.add_message("system", "😱 [grabbed + shaken]")
    _you("What about now?")
    _dash.add_message("bob", "Put me down! Someone help me!")
    _speak_reply("Put me down! Someone help me!", "sad")
    for _ in range(_RPS_SHIVER_TIMES):
        if _stopped():
            return
        _robot.set_expression("sad")     # hold the crying face across the shivers
        _play_named_move(_RPS_SHIVER_MOVE)
        _beat(1.6)
    if _stopped():
        return
    # a crying motion to finish (Sad droop — Foot flap reads happy on this robot)
    _robot.set_expression("sad")
    _play_named_move(_RPS_CRY_MOVE)
    _robot.set_expression("sad")

    _log("game", "RPS skit complete — Bob wins and melts down")


# Phrases that trigger the demo game from voice / typed chat.
_GAME_PHRASES = ("play a game", "lets play", "let's play",
                 "rock paper scissors", "rock, paper, scissors",
                 "play rock paper", "wanna play", "want to play")


def _wants_game(text: str) -> bool:
    t = (text or "").lower()
    return any(p in t for p in _GAME_PHRASES)


# Serialises skits so overlapping triggers (button + voice) can't run two
# performances at once (which would garble the TTS/screen/moves).
_game_lock = threading.Lock()


# ─────────────────────────────────────────────────────────────────────────────
#  LIQUID-EYES TILT & SHAKE DEMO — on-device eye animation + scripted voice
# ─────────────────────────────────────────────────────────────────────────────
# Drives the firmware "liquid eyes" animation (eyes slide LEFT, then RIGHT, then
# SHAKE, then turn ANGRY over ~11 s) and speaks scripted lines over it, then ends
# with an angry body move + speech. The firmware now lets /mactts through while
# the eye demo runs and FREEZES the eye-demo clock during each spoken line, so
# the eyes hold their phase while Bob yells, then resume — keeping voice roughly
# in step with the motion. While the eye demo owns the screen, ONLY audio works
# (face / ticker / move calls are refused), so during the eye phases we speak and
# nothing else; the angry finale happens after the demo is stopped.
@_robot.foreground_activity()
def _play_tiltshake_demo(stop: "threading.Event | None" = None) -> None:
    try:
        if stop is None or not stop.is_set():
            _run_tiltshake_demo(stop)
    finally:
        _robot.eyes_demo(stop=True)   # make sure the animation is ended
        _robot.set_expression("normal")
        _robot.show_screen("idle")
        _robot.robot_ticker("Ready")


def _run_tiltshake_demo(stop: "threading.Event | None" = None) -> None:
    def _stopped() -> bool:
        return stop is not None and stop.is_set()

    def _beat(secs: float) -> None:
        stop.wait(secs) if stop is not None else time.sleep(secs)

    def _say(line: str) -> None:
        # Plain speech only — no face/ticker/move while the eye demo owns the TFT.
        _tts.robot_speak(line, play_locally=PLAY_LOCALLY)

    _log("demo", "liquid-eyes demo")
    _dash.add_message("system", "🎬 Liquid eyes demo")

    # Kick off the on-device liquid-eyes animation (smooth slide left → right →
    # back to centre → violent shake → angry).
    _robot.eyes_demo()

    # Smoothly slide LEFT then RIGHT then settle — no speech, just the animation
    # (phases: left 0-2.4s, right 2.4-5.2s, back to stable by ~6.4s).
    _beat(6.3)
    if _stopped():
        return

    # Eyes are back at centre/stable → "What are you doing?"
    _dash.add_message("bob", "What are you doing?")
    _say("What are you doing?")
    if _stopped():
        return

    # Let the (now much more violent) shake play out (~6.4-8.0s).
    _beat(1.8)

    # End the animation, then ONE angry line + a kung-fu move. Nothing else.
    _robot.eyes_demo(stop=True)
    _beat(0.2)
    _dash.add_message("bob", "You have made me mad.")
    _speak_reply("You have made me mad.", "angry")
    _play_named_move("Kung fu")
    _log("demo", "liquid-eyes demo complete")


# ─────────────────────────────────────────────────────────────────────────────
#  CONVO MODE — VAD → STT → LLM → Act loop
# ─────────────────────────────────────────────────────────────────────────────
def _convo_loop(stop: threading.Event):
    """
    Background thread for CONVO mode.

    Full turn sequence:
      ticker "Listening... speak now"  / screen listen
      → VAD capture
      → ticker "Thinking..."           / screen idle
      → Whisper STT
      → Ollama LLM plan
      → set expression + ticker + screen speak + TTS + move
      → repeat or end

    On exit (stop event or user goodbye), automatically switches back to
    IDLE mode so the scared-reaction watcher re-arms.
    """
    _log("convo", "▶ convo loop started")

    # Disable scared/shake feature on the ESP32 for the duration of the conversation
    _robot.liftdetect_on(False)
    _robot.helpme_on(False)
    _log("convo", "IMU reactions suspended or unavailable in current profile")

    greeting = "Hello! I am Anvyxo. What can I do for you?"
    _dash.set_status("🗣 Speaking…")
    _dash.add_message("bob", greeting)
    _speak_reply(greeting, "happy")

    messages: list[dict] = [{"role": "assistant", "content": greeting}]
    turn = 0
    should_stop = False

    while not stop.is_set() and turn < MAX_TURNS:
        # ── LISTEN ────────────────────────────────────────────────────────────
        _robot.show_screen("listen")
        _log("convo", f"turn {turn+1}: listening ({CONVO_MIC_SOURCE} mic, VAD)")
        _dash.set_status(f"🎙 Listening — {CONVO_MIC_SOURCE} microphone…")
        _dash.set_mic_active(True)

        with _TickerKeepAlive("Listening..."):
            speech_found = _vad.capture_until_silence(
                out_path=_LISTEN_WAV,
                source=CONVO_MIC_SOURCE,
            )
        _dash.set_mic_active(False)

        if stop.is_set():
            break
        if not speech_found:
            _log("convo", "Capture failed; check robot /hw or Mac microphone permissions")
            stop.wait(2)
            continue

        # ── THINKING ──────────────────────────────────────────────────────────
        _robot.show_screen("idle")
        _dash.set_status("💭 Thinking…")
        # Gesture NOW (before speaking), not during speech: moving servos while
        # the speaker plays injects electrical noise/distortion into the audio.
        # The move runs on-device through the transcribe/LLM/upload window and is
        # finished by the time the (clean) voice actually plays.
        _play_random_big_move()

        with _TickerKeepAlive("Thinking..."):
            if not speech_found:
                transcript = ""
            else:
                transcript = _transcribe(_LISTEN_WAV)

        _log("convo", f"  heard: \"{transcript}\"")
        _dash.add_message("user", transcript)
        messages.append({"role": "user", "content": transcript})

        # ── GAME? ───────────────────────────────────────────────────────────────
        # "Let's play a game" → run the canned RPS demo inline (Bob always wins),
        # then keep listening. Skips the LLM for this turn.
        if _wants_game(transcript) and not stop.is_set():
            _log("convo", "  ↳ game request → RPS demo")
            _play_rps_demo(stop)
            messages.append({"role": "assistant",
                             "content": "We played Rock Paper Scissors and I won!"})
            turn += 1
            continue

        # ── LLM PLAN ──────────────────────────────────────────────────────────
        payload = _llm.plan_response(messages[-10:], transcript)

        reply_text         = payload["reply_text"]
        expression         = payload["expression"]
        move_cmd           = payload["move_cmd"]
        show_text          = payload["show_text"]
        should_stop        = payload["should_stop"]
        take_photo         = payload.get("take_photo", False)
        need_outside_help  = payload.get("need_outside_help", False)

        # Blank transcript fallback
        if not transcript.strip() and not should_stop:
            reply_text         = "Sorry, I didn't catch that. Could you say it again?"
            expression         = "surprised"
            move_cmd           = None
            show_text          = "Pardon?"
            need_outside_help  = False

        _log("convo", f"  reply=\"{reply_text}\" expr={expression} move={move_cmd or '—'} db2={need_outside_help}")
        messages.append({"role": "assistant", "content": reply_text})

        # ── ACT ───────────────────────────────────────────────────────────────
        _dash.set_status("🗣 Speaking…")
        _dash.add_message("bob", reply_text)
        _speak_reply(reply_text, expression, show_text)

        # ── MOVE ────────────────────────────────────────────────────────────────
        # Play the move the LLM asked for (e.g. a dance). Done AFTER speaking so the
        # servos don't inject electrical noise into the audio — robot_speak blocks
        # until playback finishes, so by here the speaker is quiet.
        if move_cmd and not stop.is_set():
            _log("convo", f"  ↳ playing move: {move_cmd}")
            _play_named_move(move_cmd)

        # ── OUTSIDE HELP (expert agents: Db2 expert or internet agent) ──────────
        # The local LLM flagged this for an expert agent and already spoke the
        # "going to my <agent>" bridge above; now query the right WatsonX agent
        # (gpt-oss) and speak the answer.
        if need_outside_help and transcript.strip() and not stop.is_set():
            _log("convo", "  ↳ Db2 expert: querying WatsonX gpt-oss …")
            _dash.set_status("📚 Asking the Db2 expert (gpt-oss)…")
            _play_named_move(DB2_MOVE)   # big move while handing off to the Db2 agent
            _robot.show_screen("idle")
            with _TickerKeepAlive("Asking Db2 expert..."):
                answer = _llm.ask_outside(transcript, kind="db2")
            _log("convo", f"  ↳ Db2 expert answer: \"{answer[:80]}\"")
            _dash.set_status("🗣 Speaking…")
            _dash.add_message("db2", answer)
            _speak_reply(answer, "happy")
            messages.append({"role": "assistant", "content": answer})

        # ── TAKE A PHOTO ────────────────────────────────────────────────────────
        # The LLM (or transcript keywords) flagged a photo request. Bob has
        # already said "smile please" above; snap the camera and save it to the
        # laptop, then carry on chatting.
        if take_photo and not stop.is_set():
            _take_photo()

        turn += 1

        if should_stop or stop.is_set():
            break

    # ── Goodbye ───────────────────────────────────────────────────────────────
    # Only play the hardcoded goodbye when the loop was stopped externally
    # (e.g. POST /mode/idle or max_turns exceeded).  When the LLM set
    # should_stop=True it already spoke its own farewell in the ACT block above,
    # so we skip the second one to avoid a double goodbye.
    if not stop.is_set() and not should_stop:
        bye = "Goodbye! Talk to you later."
        _dash.add_message("bob", bye)
        _robot.set_expression("love")
        _robot.robot_ticker("Bye!")
        _robot.show_screen("speak")
        _tts.robot_speak(bye, play_locally=PLAY_LOCALLY)

    _robot.set_expression("normal")
    _robot.show_screen("idle")
    _robot.robot_ticker("Ready")

    # Re-enable scared/shake feature before handing back to idle
    _robot.liftdetect_on(True)
    _robot.helpme_on(True)
    _log("convo", "returning to configured idle behaviour")

    _log("convo", f"✓ convo loop ended after {turn} turn(s)")

    # Auto-return to idle so scared-reaction watcher re-arms
    if not stop.is_set():
        _log("convo", "auto-returning to idle mode")
        _state.set_mode(ModeState.IDLE)
        _launch_idle()


# ─────────────────────────────────────────────────────────────────────────────
#  VISION THREAD — single owner of the robot camera (feeds the UI + presence)
# ─────────────────────────────────────────────────────────────────────────────
def _vision_loop():
    """Continuously grab camera frames for the dashboard's live stream, running
    MediaPipe face detection at ~2 Hz. Runs for the whole server lifetime and is
    the ONLY consumer of the robot camera, so the idle loop reads its face count
    instead of grabbing its own frames. Camera grabs share the robot HTTP lock
    with TTS/moves, so they naturally yield during audio uploads (no contention).
    """
    if not FACE_DETECT_ENABLED:
        _log("vision", "camera stream disabled (FACE_DETECT_ENABLED=0)")
        return
    face_ok = _face.warm_up()
    _log("vision", f"▶ camera stream {'+ MediaPipe ' if face_ok else ''}started "
                   f"(grab ~{1/CAM_GRAB_SECS:.0f}fps, detect ~{1/MP_DETECT_SECS:.0f}Hz; "
                   f"pauses while Bob speaks)")
    last_detect = 0.0
    while True:
        # Hold off entirely while audio is streaming — the link is for the
        # speaker then, and camera traffic would make the voice break up.
        if _robot.AUDIO_BUSY.is_set() or _robot.FOREGROUND_BUSY.is_set():
            time.sleep(0.1)
            continue
        t0 = time.time()
        frame = _robot.cam_capture()
        if frame:
            # Serve the RAW frame straight to the stream (fast, high FPS). The
            # browser rotates it for display. MediaPipe runs only every
            # MP_DETECT_SECS (~2 Hz, face COUNT only) so detection never throttles
            # the stream.
            _dash.set_frame(frame)
            now = time.time()
            if face_ok and now - last_detect >= MP_DETECT_SECS:
                _dash.set_faces(_face.detect(frame))
                last_detect = now
        dt = time.time() - t0
        if dt < CAM_GRAB_SECS:
            time.sleep(CAM_GRAB_SECS - dt)


# ─────────────────────────────────────────────────────────────────────────────
#  THREAD LAUNCHER
# ─────────────────────────────────────────────────────────────────────────────
def _launch_idle():
    stop = _state.stop_event
    t = threading.Thread(target=_idle_loop, args=(stop,), daemon=True)
    t.start()


_conversation_lock = threading.Lock()

def _launch_convo():
    stop = _state.stop_event
    def run():
        with _conversation_lock:
            if not stop.is_set():
                _convo_loop(stop)
    t = threading.Thread(target=run, daemon=True)
    t.start()


# ─────────────────────────────────────────────────────────────────────────────
#  DEMO MODE — scripted "ANVYXO // ONLINE" intro skit
# ─────────────────────────────────────────────────────────────────────────────
# A canned performance for recording a clip. ANVYXO speaks, emotes and dances on
# cue. The human ("YOU") lines are NEVER recorded — we just flash the normal
# "recording…" UI for a beat (mic indicator + listening screen), then drop the
# scripted line into the chat so it looks exactly like a real conversation turn.
# Fully scripted: no Whisper, no LLM, no microphone.
def _demo_loop(stop: threading.Event):
    _log("demo", "▶ demo skit started")
    # Suspend the pick-up/shake reaction so it can't interrupt the performance.
    _robot.liftdetect_on(False)
    _robot.helpme_on(False)

    def move(name, dur=3.0):
        """Play a named move and hold roughly its duration so plays don't overlap."""
        if stop.is_set():
            return
        _play_named_move(name)
        stop.wait(dur)

    def you(text, listen=2.5):
        """Human turn: show the normal 'recording' UI but record NOTHING."""
        if stop.is_set():
            return
        _robot.show_screen("listen")
        _dash.set_status("🎙 Listening — your turn…")
        _dash.set_mic_active(True)
        stop.wait(listen)                 # pretend to record; the mic is untouched
        _dash.set_mic_active(False)
        _dash.add_message("user", text)
        stop.wait(0.4)

    def anvyxo(expr, text, before=(), after=(), pause=0.0):
        """ANVYXO turn: expression → optional moves → speak → optional moves."""
        if stop.is_set():
            return
        _robot.set_expression(expr)
        for m in before:
            move(*m)
        if pause:
            stop.wait(pause)
        _dash.set_status("🗣 Speaking…")
        _dash.add_message("bob", text)
        _robot.robot_ticker(text[:26])
        _robot.show_screen("speak")
        _tts.robot_speak(text, play_locally=PLAY_LOCALLY)   # servos are still; clean audio
        for m in after:
            move(*m)

    _dash.add_message("system", "🎬 ANVYXO // demo starting")

    # 1 — boots up, discovers it has a body
    anvyxo("surprised", "Wait... you gave me a body?", before=[("Curious", 3.0)])
    you("Yeah. Try moving.")

    # 2 — tries moving: looks around, wiggles, takes a tiny step
    anvyxo("happy", "Okay... this is kinda cool.",
           before=[("Curious", 2.5), ("Wiggle", 2.5), ("Waddle", 3.0)])
    you("Don't get too excited.")

    # 3 — immediately starts dancing
    anvyxo("happy", "Too late.", after=[("Cha-cha", 6.0)])
    you("Bro, I said move. Not dance.")

    # 4 — stops, annoyed, blames you
    anvyxo("angry", "You built me. This is your fault.", pause=0.6)
    you("Fair. One last test — show me your best move.", listen=3.0)

    # 5 — the money shot: confident pause, then the cleanest dance
    if not stop.is_set():
        _robot.set_expression("happy")
        _robot.show_screen("speak")
        _robot.robot_ticker("Best move...")
        stop.wait(1.0)                    # dramatic pause
        move("Foot dance", 7.0)
    anvyxo("love", "Post that one.")

    # Outro card
    if not stop.is_set():
        _dash.add_message("system", "ANVYXO // ONLINE ✓")
        _robot.robot_ticker("ANVYXO // ONLINE")
        _robot.set_expression("happy")
        stop.wait(2.0)

    # Settle back to a neutral idle
    _robot.set_expression("normal")
    _robot.show_screen("idle")
    _robot.robot_ticker("Ready")
    _robot.liftdetect_on(True)
    _robot.helpme_on(True)
    _log("demo", "✓ demo skit ended")
    if not stop.is_set():
        _state.set_mode(ModeState.IDLE)
        _launch_idle()


def _launch_demo():
    stop = _state.stop_event
    def run():
        with _conversation_lock:
            if not stop.is_set():
                _demo_loop(stop)
    threading.Thread(target=run, daemon=True).start()


# ─────────────────────────────────────────────────────────────────────────────
#  HTTP MODE-SWITCH API
# ─────────────────────────────────────────────────────────────────────────────
class _Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):   # suppress default access log; use _log instead
        pass

    def _send_json(self, code: int, obj: dict):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_bytes(self, code: int, ctype: str, body: bytes):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    # ── GET ───────────────────────────────────────────────────────────────────
    def do_GET(self):
        parts = urllib.parse.urlsplit(self.path)
        path  = parts.path
        query = urllib.parse.parse_qs(parts.query)

        if path == "/":
            body = (Path(__file__).parent / "ui.html").read_bytes()
            self._send_bytes(200, "text/html; charset=utf-8", body)

        elif path in ("/imu-view", "/imu-view.js"):
            filename = "imu.html" if path == "/imu-view" else "imu-view.js"
            ctype = "text/html" if path == "/imu-view" else "application/javascript"
            self._send_bytes(200, ctype + "; charset=utf-8",
                             (Path(__file__).parent / filename).read_bytes())

        elif path == "/api/imu":
            # Read-only viewer yields to speech/game and existing idle work.
            with _robot.idle_activity() as acquired:
                if not acquired:
                    self._send_json(200, {"ok": False, "busy": True})
                else:
                    try:
                        data = _robot.robot_request("GET", "/imu", timeout=(1, 2)).json()
                        self._send_json(200, data)
                    except Exception:
                        self._send_json(503, {"ok": False, "error": "IMU unavailable"})

        elif path == "/mode":
            self._send_json(200, {"mode": _state.mode})

        elif path == "/health":
            self._send_json(200, {"ok": True, "mode": _state.mode,
                                  "mic_source": _active_mic(),
                                  "robot_host": _robot.ROBOT_HOST})

        elif path == "/events":
            # Dashboard polls this: chat log (since last id), status, face count.
            try:
                since = int(query.get("since", ["0"])[0])
            except ValueError:
                since = 0
            payload = _dash.events(since)
            mode = _state.mode
            payload.update({
                "mode": mode,
                "mic":  _active_mic(),
                "listening_for_wake": mode == ModeState.IDLE and WAKE_WORD_ENABLED,
                "wake_words": list(WAKE_WORDS),
                "audio_busy": _robot.AUDIO_BUSY.is_set(),   # UI pauses the stream then
                "robot_host": _robot.ROBOT_HOST,            # for the native :81 MJPEG
            })
            self._send_json(200, payload)

        elif path == "/cam/frame.jpg":
            frame = _dash.frame()
            if frame:
                self._send_bytes(200, "image/jpeg", frame)
            else:
                # No frame yet — 204 keeps the <img> quiet until one arrives.
                self.send_response(204)
                self.end_headers()

        elif path == "/cam/stream":
            # Constant MJPEG feed: repeats the vision thread's latest frame at
            # CAM_STREAM_FPS so the browser <img> never blanks. MediaPipe boxes
            # (baked into the frame) update at ~2 Hz; the video stays smooth.
            self.send_response(200)
            self.send_header("Age", "0")
            self.send_header("Cache-Control", "no-cache, private")
            self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
            self.end_headers()
            period = 1.0 / max(1.0, CAM_STREAM_FPS)
            try:
                while True:
                    frame = _dash.frame()
                    if frame:
                        self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\n")
                        self.wfile.write(f"Content-Length: {len(frame)}\r\n\r\n".encode())
                        self.wfile.write(frame)
                        self.wfile.write(b"\r\n")
                    time.sleep(period)
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass   # browser closed the stream

        else:
            self._send_json(404, {"error": "not found"})

    # ── POST ──────────────────────────────────────────────────────────────────
    def do_POST(self):
        path = self.path.rstrip("/")

        if path == "/mic":
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 256:
                    raise ValueError("invalid request size")
                source = json.loads(self.rfile.read(length)).get("source")
                if source not in ("robot", "mac"):
                    raise ValueError("source must be robot or mac")
            except (ValueError, AttributeError) as exc:
                self._send_json(400, {"error": str(exc)})
                return
            os.environ["MIC_SOURCE"] = source
            _vad.recalibrate()
            self._send_json(200, {"mic_source": source, "applies": "next recording turn"})
        elif path == "/mode/idle":
            _log("api", "POST /mode/idle")
            if _state.set_mode(ModeState.IDLE):
                _launch_idle()
            self._send_json(200, {"ok": True, "mode": "idle"})

        elif path == "/mode/convo":
            _log("api", "POST /mode/convo")
            if _state.set_mode(ModeState.CONVO):
                _launch_convo()
            self._send_json(200, {"ok": True, "mode": "convo"})

        elif path == "/mode/demo":
            # Scripted "ANVYXO // ONLINE" intro skit — no mic, fully canned.
            _log("api", "POST /mode/demo")
            if _state.set_mode(ModeState.DEMO):
                _launch_demo()
            self._send_json(200, {"ok": True, "mode": "demo"})

        elif path == "/test/scare":
            # Manually fire the pick-up/shake reaction (move + face + phrase) so
            # you can test it from the UI without physically shaking the robot.
            _log("api", "POST /test/scare")
            _scared_reaction()
            self._send_json(200, {"ok": True, "move": SCARED_MOVE})

        elif path == "/test/game":
            # Canned "Let's play a game" demo: Bob plays Rock-Paper-Scissors,
            # always throws paper, and declares victory. No camera needed.
            # Runs in a background thread so the HTTP response returns instantly
            # (the round takes several seconds of TTS). Guard against overlap.
            _log("api", "POST /test/game")
            if _game_lock.acquire(blocking=False):
                def _run_game():
                    try:
                        _play_rps_demo(_state.stop_event)
                    finally:
                        _game_lock.release()
                threading.Thread(target=_run_game, daemon=True).start()
                self._send_json(200, {"ok": True, "game": "rock-paper-scissors"})
            else:
                self._send_json(409, {"ok": False, "error": "a game is already playing"})

        elif path == "/test/tiltdemo":
            # Scripted tilt-left / tilt-right / shake performance (voice + moves)
            # so you can demo the IMU reactions without physically handling Bob.
            _log("api", "POST /test/tiltdemo")
            if _game_lock.acquire(blocking=False):
                def _run_tiltdemo():
                    try:
                        _play_tiltshake_demo(_state.stop_event)
                    finally:
                        _game_lock.release()
                threading.Thread(target=_run_tiltdemo, daemon=True).start()
                self._send_json(200, {"ok": True, "demo": "tilt-shake"})
            else:
                self._send_json(409, {"ok": False, "error": "a performance is already playing"})

        elif path == "/test/say":
            # Test / typed-chat path: inject a user message through the SAME
            # LLM → speak → (DB2 gpt-oss) pipeline the mic uses — no microphone
            # needed. Returns the routing plan so a conversation can be verified
            # end-to-end. Best used from idle (doesn't touch the mic loop).
            try:
                length = int(self.headers.get("Content-Length", "0"))
                body   = json.loads(self.rfile.read(length)) if length else {}
                text   = str(body.get("text", "")).strip()
            except (ValueError, json.JSONDecodeError) as exc:
                self._send_json(400, {"error": str(exc)}); return
            if not text:
                self._send_json(400, {"error": "text required"}); return
            _log("api", f"POST /test/say: {text!r}")

            # "Let's play a game" → canned RPS demo instead of the LLM pipeline.
            if _wants_game(text):
                _dash.add_message("user", text)
                if _game_lock.acquire(blocking=False):
                    def _run_game():
                        try:
                            _play_rps_demo(_state.stop_event)
                        finally:
                            _game_lock.release()
                    threading.Thread(target=_run_game, daemon=True).start()
                    self._send_json(200, {"heard": text, "game": "rock-paper-scissors"})
                else:
                    self._send_json(200, {"heard": text, "game": "already playing"})
                return

            t0_llm = time.time()
            plan  = _llm.plan_response([{"role": "user", "content": text}], text)
            latency_ms = (time.time() - t0_llm) * 1000
            reply = plan.get("reply_text", "")
            _dash.add_message("user", text)
            _dash.add_message("bob", reply)
            _speak_reply(reply, plan.get("expression", "normal"),
                         plan.get("show_text", ""))
            out = {"heard": text, "plan": plan, "latency_ms": latency_ms}
            if plan.get("need_outside_help"):
                _dash.set_status("📚 Asking the Db2 expert (gpt-oss)…")
                _play_named_move(DB2_MOVE)
                ans = _llm.ask_outside(text, kind="db2")
                _dash.add_message("db2", ans)
                _speak_reply(ans, "happy")
                out["db2_answer"] = ans
            self._send_json(200, out)

        # ── Runtime config endpoints (live override without restart) ──────────
        elif path == "/config/llm":
            # Change the LLM backend/model at runtime. Takes effect immediately.
            try:
                length = int(self.headers.get("Content-Length", "0"))
                body   = json.loads(self.rfile.read(length)) if length else {}
                backend = str(body.get("backend", "ollama")).strip().lower()
                model   = str(body.get("model", "")).strip()
            except (ValueError, json.JSONDecodeError) as exc:
                self._send_json(400, {"error": str(exc)}); return
            if backend == "ollama" and model:
                os.environ["OLLAMA_MODEL"] = model
                _llm.OLLAMA_MODEL  = model
                _llm.USE_OLLAMA   = True
                _log("config", f"LLM → ollama/{model}")
                self._send_json(200, {"ok": True, "backend": "ollama", "model": model})
            elif backend == "watsonx" and model:
                os.environ["LLM_MODEL"] = model
                _llm.LLM_MODEL   = model
                _llm.USE_OLLAMA  = False
                _log("config", f"LLM → watsonx/{model}")
                self._send_json(200, {"ok": True, "backend": "watsonx", "model": model})
            else:
                self._send_json(400, {"error": "backend must be ollama|watsonx, model must be non-empty"})

        elif path == "/config/tts":
            # Change TTS engine/voice/rate at runtime + play a test phrase.
            try:
                length = int(self.headers.get("Content-Length", "0"))
                body   = json.loads(self.rfile.read(length)) if length else {}
                engine      = str(body.get("engine", "say")).strip().lower()
                voice       = str(body.get("voice", "Samantha")).strip()
                rate        = int(body.get("rate", 16000))
                play_locally = bool(body.get("play_locally", False))
            except (ValueError, json.JSONDecodeError) as exc:
                self._send_json(400, {"error": str(exc)}); return
            if engine not in ("say", "piper"):
                self._send_json(400, {"error": "engine must be say or piper"}); return
            _tts.TTS_ENGINE = engine
            _tts.TTS_VOICE  = voice
            _tts._RATE      = rate
            os.environ["TTS_ENGINE"] = engine
            os.environ["TTS_VOICE"]  = voice
            os.environ["TTS_RATE"]   = str(rate)
            global PLAY_LOCALLY
            PLAY_LOCALLY = play_locally
            _log("config", f"TTS → {engine}/{voice} @{rate}Hz play_locally={play_locally}")
            # Play a short test phrase so the user hears the new voice immediately
            threading.Thread(
                target=lambda: _tts.robot_speak("Hello! I am Anvyxo.", play_locally=play_locally),
                daemon=True,
            ).start()
            self._send_json(200, {"ok": True, "engine": engine, "voice": voice,
                                  "rate": rate, "play_locally": play_locally})

        elif path == "/config/robot":
            # Switch the robot target host (or enable mock mode) at runtime.
            try:
                length = int(self.headers.get("Content-Length", "0"))
                body   = json.loads(self.rfile.read(length)) if length else {}
                host     = str(body.get("host", "")).strip().rstrip("/")
                use_mock = bool(body.get("use_mock", False))
            except (ValueError, json.JSONDecodeError) as exc:
                self._send_json(400, {"error": str(exc)}); return
            if not host:
                self._send_json(400, {"error": "host required"}); return
            _robot.ROBOT_BASE_URL = host if "://" in host else f"http://{host}"
            _robot.ROBOT_HOST     = host
            os.environ["ROBOT_HOST"] = host
            _log("config", f"Robot → {_robot.ROBOT_BASE_URL} (mock={use_mock})")
            # Try to fetch /movelist from the new host and re-register moves
            threading.Thread(target=_init_moves, daemon=True).start()
            self._send_json(200, {"ok": True, "host": host, "mock": use_mock,
                                  "base_url": _robot.ROBOT_BASE_URL})

        elif path == "/test/start_mock":
            # Start the static_robo.py mock server on port 8092 (as a subprocess).
            import socket as _socket
            mock_port = int(os.getenv("MOCK_ROBO_PORT", "8092"))
            static_robo = Path(__file__).parent / "static_robo.py"
            if not static_robo.exists():
                self._send_json(404, {"error": "static_robo.py not found"}); return
            # Check if already running
            try:
                with _socket.create_connection(("127.0.0.1", mock_port), timeout=0.5):
                    self._send_json(200, {"ok": True, "port": mock_port, "already_running": True})
                    return
            except (ConnectionRefusedError, OSError):
                pass
            try:
                venv_py = Path(__file__).parent / ".venv" / "bin" / "python"
                py = str(venv_py) if venv_py.exists() else sys.executable
                subprocess.Popen(
                    [py, str(static_robo), "--port", str(mock_port)],
                    stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                )
                _log("api", f"started static_robo.py on port {mock_port}")
                self._send_json(200, {"ok": True, "port": mock_port, "started": True})
            except Exception as exc:
                self._send_json(500, {"error": str(exc)})

        elif path == "/test/robot_status":
            # Return robot connectivity status (real or mock).
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if length: self.rfile.read(length)
            except Exception:
                pass
            status = _robot.robot_status()
            # Annotate whether it looks like a mock server
            status["mock"] = ("mock" in str(status.get("hardware", {})) or
                              "localhost" in _robot.ROBOT_HOST or
                              "8092" in _robot.ROBOT_HOST)
            self._send_json(200, status)

        elif path == "/test/benchmark":
            # Run one of three benchmarks in a background thread and return results.
            try:
                length = int(self.headers.get("Content-Length", "0"))
                body   = json.loads(self.rfile.read(length)) if length else {}
                kind        = str(body.get("kind", "llm")).strip().lower()
                quick       = bool(body.get("quick", True))
                play_locally = bool(body.get("play_locally", False))
            except (ValueError, json.JSONDecodeError) as exc:
                self._send_json(400, {"error": str(exc)}); return

            if kind == "llm":
                results = _run_llm_benchmark(quick=quick)
                self._send_json(200, {"ok": True, "kind": "llm", "results": results})

            elif kind == "tts":
                results = _run_tts_benchmark(quick=quick, play_locally=play_locally)
                self._send_json(200, {"ok": True, "kind": "tts", "results": results})

            elif kind == "sim":
                results = _run_sim_benchmark(quick=quick)
                self._send_json(200, {"ok": True, "kind": "sim", "results": results})

            else:
                self._send_json(400, {"error": f"unknown kind {kind!r}; use llm|tts|sim"})

        else:
            self._send_json(404, {"error": "not found",
                                  "hint": "POST /mode/idle or /mode/convo"})


# ─────────────────────────────────────────────────────────────────────────────
#  BENCHMARK HELPERS (called from POST /test/benchmark)
# ─────────────────────────────────────────────────────────────────────────────
def _run_llm_benchmark(quick: bool = True) -> dict:
    """Run quick LLM latency test against the currently configured backend.

    Uses the first BENCH_PROMPTS_QUICK prompts (3) in quick mode, all 14 otherwise.
    Returns a dict keyed by  "ollama/<model>"  or  "watsonx/<model>"  with p50 stats.
    """
    import requests as _req
    prompts_quick = [
        "Hi, how are you today?",
        "Tell me a quick joke!",
        "Goodbye!",
    ]
    prompts_full  = prompts_quick + [
        "What is the capital of France?",
        "Can you do a little dance?",
        "How do I run RUNSTATS on Db2?",
    ]
    prompts = prompts_quick if quick else prompts_full
    results: dict = {}

    # ── Ollama ────────────────────────────────────────────────────────────────
    if _llm.USE_OLLAMA:
        key = f"ollama/{_llm.OLLAMA_MODEL}"
        latencies = []
        for text in prompts:
            msgs = [{"role":"user","content":text}]
            t0 = time.perf_counter()
            try:
                _llm._ollama(msgs, text)
                latencies.append((time.perf_counter()-t0)*1000)
            except Exception:
                pass
        if latencies:
            latencies.sort()
            results[key] = {
                "p50_ms": latencies[len(latencies)//2],
                "p95_ms": latencies[int(len(latencies)*0.95)],
                "count":  len(latencies),
            }
    # ── Watsonx ───────────────────────────────────────────────────────────────
    if not _llm.USE_OLLAMA or not results:
        key = f"watsonx/{_llm.LLM_MODEL}"
        latencies = []
        for text in prompts:
            msgs = [{"role":"user","content":text}]
            t0 = time.perf_counter()
            try:
                _llm._watsonx(msgs, text)
                latencies.append((time.perf_counter()-t0)*1000)
            except Exception:
                pass
        if latencies:
            latencies.sort()
            results[key] = {
                "p50_ms": latencies[len(latencies)//2],
                "p95_ms": latencies[int(len(latencies)*0.95)],
                "count":  len(latencies),
            }
    return results


def _run_tts_benchmark(quick: bool = True, play_locally: bool = False) -> dict:
    """Render 1-3 short phrases and return per-phrase timing.

    No robot required — audio is either discarded (to mock /mactts) or played
    locally via afplay if play_locally=True.
    """
    import tempfile
    phrases = [
        "Hi, I am Anvyxo!",
        "Sure, let's dance!",
    ] if quick else [
        "Hi, I am Anvyxo!",
        "Sure, let's dance!",
        "Great Db2 question, let me ask my Db2 expert agent right away!",
        "I am a cheerful robot who loves to chat about science and technology.",
    ]
    results: dict = {}
    for phrase in phrases:
        key = f"tts/{phrase[:30]}"
        with tempfile.TemporaryDirectory(prefix="bob-bench-") as td:
            tmp = Path(td)
            t0 = time.perf_counter()
            try:
                src = _tts._render_to_aiff_or_wav(phrase, tmp)
                pcm = _tts._to_pcm(src)
                wav = tmp / "out.wav"
                dur = _tts.write_pcm_wav(pcm, wav)
                total_ms = (time.perf_counter()-t0)*1000
                if play_locally:
                    subprocess.run(["afplay", str(wav)], check=False,
                                   timeout=dur+10)
                results[key] = {
                    "total_ms": total_ms,
                    "p50_ms":   total_ms,
                    "dur_s":    dur,
                    "chars":    len(phrase),
                }
            except Exception as exc:
                results[key] = {"total_ms": 0, "p50_ms": 0, "error": str(exc)}
    return results


def _run_sim_benchmark(quick: bool = True) -> dict:
    """Run the scripted conversation simulation with mocked LLM responses.

    Points robot at the current ROBOT_BASE_URL (could be the mock or real robot).
    Uses mocked LLM responses (no Ollama/Watsonx calls) so the benchmark purely
    measures the TTS + robot-action pipeline latency.
    """
    from unittest.mock import patch
    import tempfile

    script = [
        "Hi, how are you?",
        "Can you do a happy wiggle?",
        "Goodbye!",
    ] if quick else [
        "Hi, how are you?",
        "Tell me a joke!",
        "Can you do a happy wiggle?",
        "How do I run RUNSTATS?",
        "Goodbye!",
    ]
    mock_plans = [
        {"reply_text": "Hello! I am Anvyxo.", "expression": "happy",
         "move_cmd": None, "show_text": "Hello!", "should_stop": False,
         "take_photo": False, "need_outside_help": False},
        {"reply_text": "Sure, let's wiggle!", "expression": "happy",
         "move_cmd": "Happy wiggle", "show_text": "Wiggling!",
         "should_stop": False, "take_photo": False, "need_outside_help": False},
        {"reply_text": "Goodbye!", "expression": "love",
         "move_cmd": None, "show_text": "Bye!", "should_stop": True,
         "take_photo": False, "need_outside_help": False},
    ] * 3   # enough for any script length

    results: dict = {}
    plan_iter = iter(mock_plans)

    with patch.object(_llm, "plan_response",
                      side_effect=lambda *a, **kw: next(plan_iter)):
        for i, text in enumerate(script):
            with tempfile.TemporaryDirectory(prefix="bob-sim-") as td:
                tmp = Path(td)
                t0 = time.perf_counter()
                try:
                    plan = _llm.plan_response([{"role":"user","content":text}], text)
                    reply = plan.get("reply_text","")
                    # TTS render only (no upload in sim benchmark)
                    with patch.object(_tts, "_send_wav"):
                        _tts.robot_speak(reply, play_locally=PLAY_LOCALLY)
                    total_ms = (time.perf_counter()-t0)*1000
                    results[f"turn{i+1}/{text[:20]}"] = {
                        "total_ms": total_ms, "p50_ms": total_ms,
                    }
                except Exception as exc:
                    results[f"turn{i+1}/{text[:20]}"] = {
                        "total_ms": 0, "p50_ms": 0, "error": str(exc),
                    }
    return results


# ─────────────────────────────────────────────────────────────────────────────
#  ENTRY POINT
# ─────────────────────────────────────────────────────────────────────────────
def main():
    _log("main", f"BOB_ROBO_CONVO starting on port {PORT}")
    _log("main", f"Robot host : {_robot.ROBOT_BASE_URL}")
    _log("main", f"Ollama URL : {os.getenv('OLLAMA_BASE_URL','http://localhost:11434')}")
    _log("main", f"Whisper    : {WHISPER_MODEL}")

    # Pre-load Whisper model NOW so first convo turn has zero download delay
    _log("main", f"Microphone : {os.getenv('MIC_SOURCE', 'robot')} (change at http://localhost:{PORT})")
    _load_whisper()

    # Pre-load the gesture model so the first RPS round has no delay
    _log("main", f"Camera     : GET {_robot.ROBOT_BASE_URL}/cam/snapshot"
                 f"  (BOB V2 built-in OV2640 — no separate CAM_HOST)")
    _gesture.warm_up()

    # Pre-start the face detector so idle-mode presence greetings work instantly
    if FACE_DETECT_ENABLED:
        _log("main", "Face detect: warming MediaPipe face detector for presence greetings")
        _face.warm_up()
        # Single camera owner: streams to the dashboard + drives presence.
        threading.Thread(target=_vision_loop, daemon=True).start()

    # Teach the LLM the robot's expressive move library (names → /playmove).
    _init_moves()

    # Boot into idle mode
    _state.set_mode(ModeState.IDLE)
    _launch_idle()

    httpd = ThreadingHTTPServer(("0.0.0.0", PORT), _Handler)
    httpd.daemon_threads = True   # concurrent dashboard polling + frame requests
    _log("main", f"listening — dashboard:  http://localhost:{PORT}/   "
                 f"(also http://<this-mac-ip>:{PORT}/)")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        _log("main", "shutting down")
        _state.stop_event.set()


if __name__ == "__main__":
    main()
