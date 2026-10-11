#!/usr/bin/env python3
"""
mac_tts_server.py — local Mac TTS helper for the Otto robot UI
==============================================================
Runs a tiny HTTP server on port 8756 (localhost only).
The robot web UI calls it, it synthesises speech with macOS `say` + ffmpeg,
and returns raw WAV bytes that the browser then POSTs to the robot.

START:
    python3 Bob_ep32Code/mac_tts_server.py

STOP:
    Ctrl-C

REQUIRES:
    • macOS (uses `say` command)
    • ffmpeg — install once:  brew install ffmpeg

VOICES (pass ?voice=Name):
    Samantha   — US English (default), natural and clear
    Rishi      — Indian English, good for Hinglish
    Karen      — Australian English
    Daniel     — British English
    Lekha      — Hindi female  (must install in System Settings → Accessibility → Spoken Content)
    Kanya      — Hindi female  (newer macOS)
    List all:  say -v '?' in Terminal

QUERY PARAMS:
    text    — (required) UTF-8 text to speak
    voice   — voice name (default: Samantha)
    rate    — output sample rate in Hz (default: 22050)

EXAMPLE (manual curl test):
    curl "http://127.0.0.1:8756/tts?text=Hello+robot&voice=Samantha" -o test.wav
    afplay test.wav
"""

import http.server
import subprocess
import tempfile
import os
import sys
import urllib.parse
import shutil

PORT = 8756
DEFAULT_VOICE = "Samantha"
DEFAULT_RATE  = "22050"


class TTSHandler(http.server.BaseHTTPRequestHandler):

    def log_message(self, fmt, *args):
        # Quieter logs — only print on errors
        pass

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)

        if parsed.path != "/tts":
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"not found")
            return

        params = urllib.parse.parse_qs(parsed.query)
        text  = params.get("text",  [""])[0].strip()
        voice = params.get("voice", [DEFAULT_VOICE])[0].strip()
        rate  = params.get("rate",  [DEFAULT_RATE])[0].strip()

        if not text:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b"missing text parameter")
            return

        # Validate rate
        try:
            rate_int = int(rate)
            if rate_int < 4000 or rate_int > 48000:
                rate_int = 22050
        except ValueError:
            rate_int = 22050

        print(f"  voice={voice!r}  rate={rate_int}  text={text!r}")

        # Check ffmpeg is available
        if not shutil.which("ffmpeg"):
            self.send_error_cors(500, "ffmpeg not found — run: brew install ffmpeg")
            return

        try:
            wav_bytes = synthesise(text, voice, rate_int)
        except Exception as exc:
            print(f"  ERROR: {exc}", file=sys.stderr)
            self.send_error_cors(500, str(exc))
            return

        # Send WAV with CORS headers so the browser fetch() can read the body
        self.send_response(200)
        self.send_header("Content-Type", "audio/wav")
        self.send_header("Content-Length", str(len(wav_bytes)))
        self.send_header("Access-Control-Allow-Origin", "*")   # allows browser fetch
        self.end_headers()
        self.wfile.write(wav_bytes)
        print(f"  → {len(wav_bytes)} bytes sent")

    def do_OPTIONS(self):
        """CORS pre-flight — browsers send this before cross-origin fetch."""
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.end_headers()

    def send_error_cors(self, code, msg):
        body = msg.encode()
        self.send_response(code)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)


def synthesise(text: str, voice: str, rate: int) -> bytes:
    """
    Run macOS `say` → AIFF, then ffmpeg → 16-bit mono WAV at `rate` Hz.
    Returns the WAV file contents as bytes.
    """
    with tempfile.TemporaryDirectory() as tmp:
        aiff = os.path.join(tmp, "tts.aiff")
        wav  = os.path.join(tmp, "tts.wav")

        # macOS say — always writes AIFF natively
        result = subprocess.run(
            ["say", "-v", voice, "-o", aiff, text],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            raise RuntimeError(f"say failed: {result.stderr.strip() or 'unknown error'}")
        if not os.path.exists(aiff) or os.path.getsize(aiff) == 0:
            raise RuntimeError(f"say produced no output — is voice '{voice}' installed?")

        # ffmpeg: AIFF → 16-bit mono WAV at target rate, maximised to full scale
        # volume=50dB blasts the level up; alimiter hard-clips at 0.9999 (≈±32767)
        result = subprocess.run(
            [
                "ffmpeg", "-y", "-loglevel", "error",
                "-i", aiff,
                "-ar", str(rate),
                "-ac", "1",
                "-af", "volume=50dB,alimiter=level_in=1:level_out=1:limit=0.9999:attack=5:release=50",
                "-acodec", "pcm_s16le",
                wav,
            ],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            raise RuntimeError(f"ffmpeg failed: {result.stderr.strip()}")

        with open(wav, "rb") as f:
            return f.read()


if __name__ == "__main__":
    server = http.server.HTTPServer(("127.0.0.1", PORT), TTSHandler)
    print(f"Mac TTS helper running on http://127.0.0.1:{PORT}")
    print(f"Default voice: {DEFAULT_VOICE}  |  Default rate: {DEFAULT_RATE} Hz")
    print(f"Test:  curl 'http://127.0.0.1:{PORT}/tts?text=Hello+robot' -o /tmp/test.wav && afplay /tmp/test.wav")
    print(f"Stop:  Ctrl-C\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
