#!/usr/bin/env python3
"""
test_tts_conversation.py — scripted TTS conversation test for BOB.

Sends a sequence of lines to the robot speaker using the exact same
robot_speak() → POST /mactts path that the live conversation agent uses.
Tests different sentence structures, lengths, and pacing to exercise
the chunking pipeline end-to-end.

Run from BOB_ROBO_CONVO/:
    .venv/bin/python3 tests/test_tts_conversation.py
"""
from __future__ import annotations

import sys
import os
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "lib"))

from dotenv import load_dotenv
load_dotenv(dotenv_path=Path(__file__).parent.parent / ".env", override=True)

from robo_tts import robot_speak, TTS_VOICE, TTS_ENGINE, _RATE, TTS_SPEECH_RATE, TTS_BASE_URL, TTS_ENDPOINT, CHUNK_CHARS

# ── Scripted conversation lines ───────────────────────────────────────────────
# Each entry is (label, text).
# Designed to cover: short burst, medium sentence, multi-chunk long reply,
# numbers/emphasis, question cadence, excited tone.
LINES = [
    ("greeting",       "Hey! I'm Anvyxo. Nice to meet you!"),
    ("short burst",    "Beep boop. I am a robot."),
    ("medium",         "My favourite thing is when humans ask me hard questions. I love showing off."),
    ("multi-chunk",    "Did you know that the ESP32-S3 has eight megabytes of PSRAM? "
                       "That is half a million bytes of space for buffering audio, "
                       "which is exactly how I play back speech without any stuttering."),
    ("numbers",        "I was built in twenty twenty four and I already know over one thousand facts."),
    ("question",       "So, what would you like to talk about today? I am all ears — well, microphone."),
    ("excited",        "Oh wow, that is amazing! Tell me more!"),
    ("sign-off",       "Thanks for testing my voice. Everything sounds crystal clear. Goodbye for now!"),
]

def main() -> None:
    print()
    print("═" * 60)
    print("  BOB TTS Conversation Test")
    print(f"  Voice : {TTS_VOICE}  ({TTS_ENGINE})  {_RATE} Hz  {TTS_SPEECH_RATE} wpm")
    print(f"  Path  : {TTS_BASE_URL}{TTS_ENDPOINT}  chunk={CHUNK_CHARS} chars")
    print(f"  Output: {os.getenv('TTS_OUTPUT_GAIN', '1.0')} amplitude after limiter")
    print("═" * 60)
    print()

    passed = 0
    failed = 0

    for i, (label, text) in enumerate(LINES, 1):
        print(f"[{i}/{len(LINES)}] {label}")
        print(f"        \"{text}\"")
        t0 = time.monotonic()
        ok = robot_speak(text)
        elapsed = time.monotonic() - t0
        status = "✓" if ok else "✗ FAILED"
        print(f"        → {status}  ({elapsed:.1f}s total)")
        print()
        if ok:
            passed += 1
        else:
            failed += 1
        # Brief pause between lines so the robot's WiFi stack fully releases
        # before the next POST. Not strictly needed (mactts is synchronous) but
        # gives a natural conversation rhythm.
        if i < len(LINES):
            time.sleep(0.3)

    print("═" * 60)
    print(f"  Result: {passed}/{len(LINES)} HTTP passes" + (f"  ({failed} FAILED)" if failed else ""))
    print("  Audible quality requires a listening check; HTTP success does not prove smooth audio.")
    print("═" * 60)
    print()
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
