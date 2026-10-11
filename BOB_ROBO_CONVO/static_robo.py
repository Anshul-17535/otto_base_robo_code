#!/usr/bin/env python3
"""
static_robo.py — Mock ESP32 robot server for offline conversation testing.

Runs a lightweight HTTP server on port 8092 that mimics the BOB V2 firmware API.
Every endpoint returns valid JSON/bytes so server.py and the lib/* helpers work
without a real ESP32.  All audio is consumed and discarded; TTS audio that arrives
via POST /mactts is ALSO played back through the Mac speaker via afplay so you can
hear the full conversation without hardware.

Usage:
    cd BOB_ROBO_CONVO
    .venv/bin/python static_robo.py            # default port 8092
    .venv/bin/python static_robo.py --port 8093

Then set ROBOT_HOST=localhost:8092 in your .env and run server.py normally.

Simulated behaviours
─────────────────────
  /eyes?m=N       → log expression change, return 200
  /ticker?text=X  → log ticker text, return 200
  /screen/MODE    → log screen mode, return 200
  /move?cmd=X     → log move, simulate short delay, return 200
  /playmove?n=N   → log library move by name, simulate delay, return 200
  /movelist       → return the full 31-move list from the real firmware
  /mactts         → receive WAV upload, decode header, play via afplay, return 200
  /record         → return a short silence WAV (for robot-mic wake word tests)
  /state          → return neutral IMU state (no scared/shake/tilt)
  /hw             → return mock hardware status JSON
  /health         → return {"ok": true}
  /liftdetect     → accept and log
  /helpme         → accept and log
  /rps/go         → log, return 200
  /rps/reveal     → return {"pick": "paper"}
  /eyes/demo      → log, return 200
  /cam/snapshot   → return a 1×1 grey JPEG
  /savepose       → return 200
  /servomap       → GET returns current map; POST updates it
  /servopos       → accept and log
"""
from __future__ import annotations

import argparse
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import threading
import time
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

# ── Move library (matches movements_library.h baked into firmware) ─────────────
_MOVES = [
    {"n": 0,  "name": "Breathe"},
    {"n": 1,  "name": "Little nod"},
    {"n": 2,  "name": "Look around"},
    {"n": 3,  "name": "Sleepy rock"},
    {"n": 4,  "name": "Curious"},
    {"n": 5,  "name": "Curious lean"},
    {"n": 6,  "name": "Peek"},
    {"n": 7,  "name": "Sneaky tiptoe"},
    {"n": 8,  "name": "Happy wiggle"},
    {"n": 9,  "name": "Wiggle"},
    {"n": 10, "name": "Excited hop"},
    {"n": 11, "name": "Celebrate"},
    {"n": 12, "name": "Sad droop"},
    {"n": 13, "name": "Shiver"},
    {"n": 14, "name": "Hiccup"},
    {"n": 15, "name": "Double take"},
    {"n": 16, "name": "Bounce"},
    {"n": 17, "name": "Foot flap"},
    {"n": 18, "name": "Foot dance"},
    {"n": 19, "name": "Waddle"},
    {"n": 20, "name": "Cha-cha"},
    {"n": 21, "name": "Moonwalk slide"},
    {"n": 22, "name": "Leg wave"},
    {"n": 23, "name": "Robot"},
    {"n": 24, "name": "Dizzy"},
    {"n": 25, "name": "Sneeze"},
    {"n": 26, "name": "Kung fu"},
    {"n": 27, "name": "Tantrum"},
    {"n": 28, "name": "Kick"},
    {"n": 29, "name": "One leg stand"},
    {"n": 30, "name": "Walk forward"},
    # Extra names matching CONVO moves from server.py
    {"n": 16, "name": "Bounce (sync)"},
]
_MOVE_BY_IDX = {m["n"]: m["name"] for m in _MOVES}

# ── Servo map state ────────────────────────────────────────────────────────────
_servo_map = {"LH": 38, "LF": 40, "RH": 39, "RF": 41}

# ── Silence WAV (16 kHz mono PCM16 — 1 s of zeros) ───────────────────────────
def _silence_wav(secs: float = 1.0) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(16000)
        wf.writeframes(b"\x00\x00" * int(16000 * secs))
    return buf.getvalue()

_SILENCE_WAV = _silence_wav(1.0)

# ── 1×1 grey JPEG ─────────────────────────────────────────────────────────────
# Minimal valid JPEG for /cam/snapshot
_GREY_JPEG = (
    b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
    b"\xff\xdb\x00C\x00\x08\x06\x06\x07\x06\x05\x08\x07\x07\x07\t\t\x08\n"
    b"\x0c\x14\r\x0c\x0b\x0b\x0c\x19\x12\x13\x0f\x14\x1d\x1a\x1f\x1e\x1d"
    b"\x1a\x1c\x1c $.' \",#\x1c\x1c(7),01444\x1f'9=82<.342\x1e\xff\xc0\x00"
    b"\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00\xff\xc4\x00\x1f\x00\x00\x01"
    b"\x05\x01\x01\x01\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02"
    b"\x03\x04\x05\x06\x07\x08\t\n\x0b\xff\xc4\x00\xb5\x10\x00\x02\x01\x03"
    b"\x03\x02\x04\x03\x05\x05\x04\x04\x00\x00\x01}\x01\x02\x03\x00\x04\x11"
    b"\x05\x12!1A\x06\x13Qa\x07\"q\x142\x81\x91\xa1\x08#B\xb1\xc1\x15R\xd1"
    b"\xf0$3br\x82\t\n\x16\x17\x18\x19\x1a%&'()*456789:CDEFGHIJSTUVWXYZ"
    b"cdefghijstuvwxyz\x83\x84\x85\x86\x87\x88\x89\x8a\x92\x93\x94\x95\x96"
    b"\x97\x98\x99\x9a\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xb2\xb3\xb4\xb5"
    b"\xb6\xb7\xb8\xb9\xba\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xd2\xd3\xd4"
    b"\xd5\xd6\xd7\xd8\xd9\xda\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xf1"
    b"\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xff\xda\x00\x08\x01\x01\x00\x00"
    b"?\x00\xfb\xd2\x8a(\x03\xff\xd9"
)

# ── Log ────────────────────────────────────────────────────────────────────────
_LOG_LOCK = threading.Lock()

def _log(tag: str, msg: str) -> None:
    ts = time.strftime("%H:%M:%S")
    with _LOG_LOCK:
        print(f"[{ts}] [mock-robo/{tag}] {msg}", file=sys.stderr, flush=True)

# ── Audio playback (afplay on macOS) ──────────────────────────────────────────
_PLAY_AUDIO = os.getenv("MOCK_PLAY_AUDIO", "1").strip() == "1"

def _play_wav_bytes(data: bytes) -> None:
    """Play WAV bytes on Mac speaker via afplay (non-blocking)."""
    if not _PLAY_AUDIO:
        return
    try:
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
            f.write(data)
            fpath = f.name
        def _play():
            try:
                subprocess.run(["afplay", fpath], check=False, timeout=30)
            finally:
                try:
                    os.unlink(fpath)
                except OSError:
                    pass
        threading.Thread(target=_play, daemon=True).start()
    except Exception as exc:
        _log("audio", f"afplay failed: {exc}")


class _Handler(BaseHTTPRequestHandler):
    # ── Suppress default request logging (we log ourselves) ───────────────────
    def log_message(self, fmt, *args):
        pass

    def _send(self, body: bytes, ct: str = "application/json", code: int = 200):
        self.send_response(code)
        self.send_header("Content-Type", ct)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj: object, code: int = 200):
        self._send(json.dumps(obj).encode(), ct="application/json", code=code)

    def _ok(self):
        self._json({"ok": True})

    def _read_body(self) -> bytes:
        length = int(self.headers.get("Content-Length", 0))
        return self.rfile.read(length) if length > 0 else b""

    def _parse_qs(self):
        parsed = urlparse(self.path)
        return parsed.path, parse_qs(parsed.query)

    # ── GET ────────────────────────────────────────────────────────────────────
    def do_GET(self):
        path, qs = self._parse_qs()

        if path == "/health":
            self._ok()

        elif path == "/hw":
            self._json({
                "ok": True, "mock": True,
                "wifi": {"ssid": "MockNet", "ip": "127.0.0.1", "rssi": -42},
                "heap": 200000, "psram": 4000000,
                "imu": {"ok": True}, "cam": {"ok": True},
                "mic": {"ok": True}, "speaker": {"ok": True},
            })

        elif path == "/state":
            self._json({
                "mode": "idle", "scaredMode": 0, "react": 0,
                "pitch": 0.5, "roll": -0.3,
                "ax": 0, "ay": 0, "az": 981,
                "gx": 0, "gy": 0, "gz": 0,
            })

        elif path == "/movelist":
            self._json(_MOVES)

        elif path == "/playmove":
            n = int(qs.get("n", ["0"])[0])
            name = _MOVE_BY_IDX.get(n, f"move#{n}")
            _log("move", f"▶ /playmove?n={n}  ({name})")
            # Simulate a short move duration so server.py timings feel real
            time.sleep(0.05)
            self._ok()

        elif path == "/move":
            cmd = qs.get("cmd", [""])[0]
            _log("move", f"▶ /move?cmd={cmd}")
            time.sleep(0.05)
            self._ok()

        elif path.startswith("/eyes"):
            m = qs.get("m", ["6"])[0]
            expr_names = {0:"happy",1:"sad",2:"angry",3:"surprised",4:"sleepy",5:"love",6:"normal"}
            _log("face", f"👁 expression → {expr_names.get(int(m), m)}")
            self._ok()

        elif path.startswith("/ticker"):
            text = qs.get("text", [""])[0]
            _log("ticker", f"📟 {text!r}")
            self._ok()

        elif path.startswith("/screen/"):
            mode = path.split("/screen/")[-1]
            _log("screen", f"🖥  screen → {mode}")
            self._ok()

        elif path == "/record":
            secs = int(qs.get("secs", ["1"])[0])
            _log("mic", f"🎙 /record?secs={secs} → silence WAV")
            self._send(_silence_wav(secs), ct="audio/wav")

        elif path == "/cam/snapshot":
            _log("cam", "📸 /cam/snapshot")
            self._send(_GREY_JPEG, ct="image/jpeg")

        elif path == "/liftdetect":
            on = qs.get("on", ["1"])[0]
            _log("imu", f"lift detect → {'ON' if on=='1' else 'OFF'}")
            self._ok()

        elif path == "/helpme":
            on = qs.get("on", ["1"])[0]
            _log("imu", f"helpme → {'ON' if on=='1' else 'OFF'}")
            self._ok()

        elif path == "/rps/go":
            _log("rps", "▶ countdown")
            self._ok()

        elif path == "/rps/reveal":
            self._json({"pick": "paper"})

        elif path == "/eyes/demo":
            stop = qs.get("stop", ["0"])[0] == "1"
            _log("face", f"👁 eyes demo {'stop' if stop else 'start'}")
            self._ok()

        elif path == "/savepose":
            _log("pose", f"savepose {dict(qs)}")
            self._ok()

        elif path == "/servomap":
            self._json(_servo_map)

        elif path == "/imucal":
            _log("imu", "IMU calibration (mock)")
            self._ok()

        else:
            _log("404", f"GET {path} (not mocked)")
            self._json({"error": f"not mocked: {path}"}, code=404)

    # ── POST ───────────────────────────────────────────────────────────────────
    def do_POST(self):
        path, qs = self._parse_qs()

        if path in ("/mactts", "/audio"):
            body = self._read_body()
            # multipart: find WAV boundary
            ct = self.headers.get("Content-Type", "")
            wav_bytes = body
            if "multipart" in ct:
                # Find the WAV binary: starts after the first blank line of the part
                idx = body.find(b"\r\n\r\n")
                if idx >= 0:
                    wav_bytes = body[idx + 4:]
                    # strip trailing boundary
                    end = wav_bytes.rfind(b"\r\n--")
                    if end > 0:
                        wav_bytes = wav_bytes[:end]

            # Parse WAV header to get duration
            try:
                with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
                    dur = wf.getnframes() / wf.getframerate()
                _log("audio", f"🔊 /mactts: {len(wav_bytes)} bytes  {dur:.2f}s audio → playing")
                _play_wav_bytes(wav_bytes)
            except Exception as exc:
                _log("audio", f"🔊 /mactts: {len(body)} bytes (WAV parse failed: {exc})")
                _play_wav_bytes(body)
            self._ok()

        elif path == "/servomap":
            body = self._read_body()
            try:
                updates = json.loads(body)
                _servo_map.update(updates)
                _log("servo", f"map updated → {_servo_map}")
            except Exception:
                pass
            self._json(_servo_map)

        elif path == "/mode/idle":
            _log("mode", "→ idle (mock ignores mode changes)")
            self._ok()

        else:
            _log("404", f"POST {path} (not mocked)")
            self._json({"error": f"not mocked: {path}"}, code=404)

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()


def main():
    ap = argparse.ArgumentParser(description="Mock ESP32 robot API server")
    ap.add_argument("--port", type=int, default=int(os.getenv("MOCK_ROBO_PORT", "8092")))
    ap.add_argument("--no-audio", action="store_true",
                    help="Suppress afplay playback (still receives the audio)")
    args = ap.parse_args()

    global _PLAY_AUDIO
    if args.no_audio:
        _PLAY_AUDIO = False

    server = ThreadingHTTPServer(("0.0.0.0", args.port), _Handler)
    print(f"[mock-robo] listening on http://0.0.0.0:{args.port}", flush=True)
    print(f"[mock-robo] audio playback: {'ON (afplay)' if _PLAY_AUDIO else 'OFF'}", flush=True)
    print(f"[mock-robo] set ROBOT_HOST=localhost:{args.port} in .env", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[mock-robo] stopped")


if __name__ == "__main__":
    main()
