#!/usr/bin/env python3
"""Prepare one continuous WAV; optionally compare normal/quarter amplitude.

Default is offline preparation. --play sends two clips to Bob without movement.
Both clips use exactly the same rendered samples, with no phrase chunking.
"""
from __future__ import annotations

import argparse
import array
import json
import os
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "lib"))
from dotenv import load_dotenv
load_dotenv(ROOT / ".env", override=True)
import robo_tts as tts

TEXT = "One, two, three, four, five. This is one continuous clip. Listen for breaks inside the words."


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--play", action="store_true")
    parser.add_argument("--output", type=Path, default=Path("/tmp/bob-tts-comparison"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="bob-tts-render-") as temp:
        source = tts._render_to_aiff_or_wav(TEXT, Path(temp))
        pcm = tts._to_pcm(source)
    samples = array.array("h")
    samples.frombytes(pcm)
    if sys.byteorder != "little":
        samples.byteswap()
    quiet = array.array("h", (round(x * 0.25) for x in samples))
    if sys.byteorder != "little":
        quiet.byteswap()
    clips = []
    for name, data in [("A_current_level", pcm), ("B_quarter_amplitude", quiet.tobytes())]:
        path = args.output / (name + ".wav")
        duration = tts.write_pcm_wav(data, path)
        if path.stat().st_size > 512 * 1024:
            raise RuntimeError("Diagnostic clip exceeds firmware buffer")
        clips.append((name, path, duration))
        print(f"Prepared {name}: {duration:.2f}s + {tts.TTS_PAD_MS}ms pad; {path}", flush=True)
    report = {"text": TEXT, "rate": tts._RATE, "voice": tts.TTS_VOICE,
              "configured_output_gain": float(os.getenv("TTS_OUTPUT_GAIN", "1.0")),
              "peak": max(abs(x) for x in samples) / 32768,
              "rms": (sum(x*x for x in samples) / len(samples)) ** 0.5 / 32768,
              "clips": []}
    if args.play:
        with tts.foreground_activity(), tts._SPEECH_LOCK:
            tts.AUDIO_BUSY.set()
            try:
                for i, (name, path, duration) in enumerate(clips):
                    print(f"Playing {name} on {tts.TTS_BASE_URL}{tts.TTS_ENDPOINT}", flush=True)
                    start = time.monotonic()
                    tts._send_wav(path, duration)
                    elapsed = time.monotonic() - start
                    report["clips"].append({"name": name, "audio_seconds": duration,
                                            "request_seconds": round(elapsed, 3), "http_ok": True})
                    print(f"HTTP success after {elapsed:.2f}s; audible result needs a listener", flush=True)
                    if i == 0:
                        time.sleep(2)
            finally:
                tts.AUDIO_BUSY.clear()
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
