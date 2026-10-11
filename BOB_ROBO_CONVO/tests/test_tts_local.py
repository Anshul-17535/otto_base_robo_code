#!/usr/bin/env python3
"""
test_tts_local.py — TTS engine speed benchmark + Mac local playback test.

Tests every available TTS backend (say voices + piper if installed) by:
  1. Rendering each test phrase to a WAV file
  2. Measuring render latency (synthesis + ffmpeg processing)
  3. Playing the resulting audio through the Mac speaker via afplay
  4. Verifying WAV header is correctly formed (16 kHz mono PCM16)

No robot required — audio is played locally only.

Usage:
    cd BOB_ROBO_CONVO
    .venv/bin/python tests/test_tts_local.py                     # all voices, play
    .venv/bin/python tests/test_tts_local.py --no-play           # bench only, silent
    .venv/bin/python tests/test_tts_local.py --voices Samantha,Alex  # specific voices
    .venv/bin/python tests/test_tts_local.py --rate 8000         # test lower sample rate

Environment:
    TTS_VOICE     default macOS voice (used as baseline if --voices not given)
    FFMPEG_PATH   path to ffmpeg binary
    TTS_RATE      sample rate (default 16000)
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
import time
import unittest
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

from dotenv import load_dotenv
load_dotenv(dotenv_path=Path(__file__).resolve().parents[1] / ".env", override=True)

# Patch env before importing robo_tts so it picks up our overrides
import robo_tts as tts
import robo_robot as robot

# ── Test phrases ───────────────────────────────────────────────────────────────
SHORT_PHRASES = [
    "Hi, I am Anvyxo!",
    "Sure, let's dance!",
    "Goodbye!",
]
MEDIUM_PHRASES = [
    "Great Db2 question, let me ask my Db2 expert agent right away!",
    "I am a cheerful robot who loves to chat about science, technology, and everything in between.",
]
LONG_PHRASE = (
    "Quantum computing uses qubits, which can exist in superposition. "
    "This allows quantum computers to process many possibilities at once, "
    "solving certain problems much faster than classical computers."
)

ALL_PHRASES = SHORT_PHRASES + MEDIUM_PHRASES + [LONG_PHRASE]


def _mac_voices() -> list[str]:
    """List available macOS `say` voices (English only for speed)."""
    try:
        out = subprocess.check_output(
            ["say", "--voice", "?"], text=True, timeout=5
        )
        voices = []
        for line in out.splitlines():
            # Format: "Name     language    # sample text"
            parts = line.split()
            if parts and "en_" in (parts[1] if len(parts) > 1 else ""):
                voices.append(parts[0])
        return voices[:8] if voices else ["Samantha"]   # cap at 8 for speed
    except Exception:
        return ["Samantha"]


def _render_phrase(phrase: str, voice: str, rate: int, no_play: bool = False) -> dict:
    """Render one phrase and return timing + WAV metadata."""
    with tempfile.TemporaryDirectory(prefix="bob-tts-bench-") as tmpdir:
        tmp = Path(tmpdir)

        # Step 1: synthesis
        t_synth = time.perf_counter()
        aiff = tmp / "speech.aiff"
        try:
            subprocess.run(
                ["/usr/bin/say", "-v", voice, "-o", str(aiff), phrase],
                check=True, capture_output=True, timeout=30,
            )
        except subprocess.CalledProcessError as exc:
            return {"ok": False, "error": f"say failed: {exc}",
                    "synth_ms": 0, "ffmpeg_ms": 0, "total_ms": 0, "dur_s": 0}
        synth_ms = (time.perf_counter() - t_synth) * 1000

        # Step 2: ffmpeg processing (same chain as robo_tts._to_pcm)
        t_ff = time.perf_counter()
        try:
            pcm = tts._to_pcm(aiff)
        except Exception as exc:
            return {"ok": False, "error": f"ffmpeg failed: {exc}",
                    "synth_ms": synth_ms, "ffmpeg_ms": 0, "total_ms": 0, "dur_s": 0}
        ffmpeg_ms = (time.perf_counter() - t_ff) * 1000

        # Step 3: write WAV
        wav_path = tmp / "output.wav"
        dur_s = tts.write_pcm_wav(pcm, wav_path)

        # Step 4: verify WAV
        with wave.open(str(wav_path), "rb") as wf:
            assert wf.getnchannels() == 1,  "expected mono"
            assert wf.getsampwidth() == 2,  "expected 16-bit"
            assert wf.getframerate() == rate, f"expected {rate} Hz, got {wf.getframerate()}"

        total_ms = synth_ms + ffmpeg_ms

        # Step 5: local playback
        if not no_play:
            subprocess.run(["afplay", str(wav_path)], check=False, timeout=dur_s + 10)

        return {
            "ok":        True,
            "synth_ms":  synth_ms,
            "ffmpeg_ms": ffmpeg_ms,
            "total_ms":  total_ms,
            "dur_s":     dur_s,
            "wav_bytes": wav_path.stat().st_size,
            "realtime_x": dur_s * 1000 / max(total_ms, 1),  # render speed / audio length
        }


# ── unittest integration ───────────────────────────────────────────────────────
class TTSLocalTests(unittest.TestCase):
    """Unit-test mode: checks correctness, no audio playback."""

    def test_say_renders_wav(self):
        """macOS `say` + ffmpeg pipeline produces valid 16 kHz mono PCM16 WAV."""
        result = _render_phrase(SHORT_PHRASES[0], tts.TTS_VOICE, tts._RATE, no_play=True)
        self.assertTrue(result["ok"], result.get("error"))
        self.assertGreater(result["dur_s"], 0.1)

    def test_split_phrases_roundtrip(self):
        """Splitting a long phrase and rejoining gives back the original text."""
        text = LONG_PHRASE
        chunks = tts._split_phrases(text)
        rejoined = " ".join(chunks)
        self.assertEqual(rejoined, text)

    def test_split_phrases_all_under_limit(self):
        """Every split chunk is within CHUNK_CHARS."""
        chunks = tts._split_phrases(LONG_PHRASE)
        for c in chunks:
            self.assertLessEqual(len(c), tts.CHUNK_CHARS,
                                 f"chunk too long: {c!r}")

    def test_first_chunk_under_first_limit(self):
        """The first chunk respects FIRST_CHUNK_CHARS."""
        text = "Hello there! " * 10
        chunks = tts._split_phrases(text)
        if chunks:
            self.assertLessEqual(len(chunks[0]), tts.FIRST_CHUNK_CHARS)

    def test_render_all_short_phrases(self):
        """All short test phrases render without error."""
        for phrase in SHORT_PHRASES:
            result = _render_phrase(phrase, tts.TTS_VOICE, tts._RATE, no_play=True)
            self.assertTrue(result["ok"], f"Failed on {phrase!r}: {result.get('error')}")

    def test_render_latency_under_2s_per_short_phrase(self):
        """Short phrases (< 10 words) should render in < 2000 ms on this machine."""
        phrase = SHORT_PHRASES[0]
        result = _render_phrase(phrase, tts.TTS_VOICE, tts._RATE, no_play=True)
        self.assertTrue(result["ok"])
        self.assertLess(result["total_ms"], 2000,
                        f"Render too slow: {result['total_ms']:.0f}ms for {phrase!r}")

    def test_write_pcm_wav_header(self):
        """write_pcm_wav writes a valid 44-byte PCM16 WAV header."""
        samples = tts._RATE // 10
        pcm = b"\x00\x02" * samples   # 0.1 s at the configured sample rate
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
            path = Path(f.name)
        try:
            dur = tts.write_pcm_wav(pcm, path)
            self.assertAlmostEqual(dur, 0.1, places=2)
            with wave.open(str(path), "rb") as wf:
                self.assertEqual(wf.getnchannels(), 1)
                self.assertEqual(wf.getsampwidth(), 2)
                self.assertEqual(wf.getframerate(), tts._RATE)
                self.assertEqual(wf.getnframes(), samples + int(tts._RATE * tts.TTS_PAD_MS / 1000))
        finally:
            path.unlink(missing_ok=True)


# ── Benchmark CLI ──────────────────────────────────────────────────────────────
def _run_benchmark(voices: list[str], rate: int, no_play: bool, phrases: list[str]):
    results_by_voice: dict[str, list[dict]] = {}
    total_tests = len(voices) * len(phrases)
    done = 0

    print(f"\n{'='*80}")
    print(f" TTS BENCHMARK  {len(voices)} voice(s) × {len(phrases)} phrase(s) = {total_tests} renders")
    print(f" Rate: {rate} Hz  |  Play: {'YES' if not no_play else 'NO'}")
    print(f"{'='*80}")

    for voice in voices:
        print(f"\n── Voice: {voice} ──────────────────────────────────────────────")
        results_by_voice[voice] = []
        for phrase in phrases:
            short_phrase = phrase[:55] + "…" if len(phrase) > 55 else phrase
            result = _render_phrase(phrase, voice, rate, no_play)
            results_by_voice[voice].append(result)
            done += 1
            if result["ok"]:
                rt = result["realtime_x"]
                bar = "█" * min(int(rt * 10), 30)
                print(f"  ✓ {result['total_ms']:5.0f}ms  ({result['synth_ms']:.0f}ms say + "
                      f"{result['ffmpeg_ms']:.0f}ms ffmpeg)  "
                      f"{result['dur_s']:.2f}s audio  {rt:.1f}× realtime  "
                      f"{short_phrase!r}")
            else:
                print(f"  ✗ {short_phrase!r}  ERROR: {result.get('error')}")

        # Per-voice summary
        ok_results = [r for r in results_by_voice[voice] if r["ok"]]
        if ok_results:
            latencies = sorted(r["total_ms"] for r in ok_results)
            p50 = latencies[len(latencies) // 2]
            p95 = latencies[int(len(latencies) * 0.95)]
            avg_rt = sum(r["realtime_x"] for r in ok_results) / len(ok_results)
            print(f"\n  Summary {voice}: p50={p50:.0f}ms  p95={p95:.0f}ms  "
                  f"avg realtime={avg_rt:.1f}×  "
                  f"({len(ok_results)}/{len(phrases)} ok)")

    # Overall comparison
    if len(voices) > 1:
        print(f"\n{'='*80}")
        print(" VOICE COMPARISON (p50 render latency)")
        print(f"{'='*80}")
        summaries = []
        for voice in voices:
            ok = [r for r in results_by_voice[voice] if r["ok"]]
            if ok:
                lats = sorted(r["total_ms"] for r in ok)
                p50 = lats[len(lats) // 2]
                summaries.append((p50, voice))
        for p50, voice in sorted(summaries):
            bar = "█" * max(1, int(p50 / 50))
            print(f"  {voice:<20} {p50:5.0f}ms  {bar}")
        print(f"\n  ★ Fastest: {min(summaries, key=lambda x: x[0])[1]}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-play", action="store_true",
                    help="Skip afplay (benchmark only, no audio output)")
    ap.add_argument("--voices", metavar="V1,V2,...",
                    help="Comma-separated macOS voices to test (default: auto)")
    ap.add_argument("--rate", type=int, default=tts._RATE,
                    help=f"Sample rate Hz (default: {tts._RATE})")
    ap.add_argument("--quick", action="store_true",
                    help="Only test short phrases")
    ap.add_argument("--test", action="store_true",
                    help="Run unittest suite instead of benchmark")
    args = ap.parse_args()

    if args.test:
        # Run unit tests instead of interactive benchmark
        unittest.main(argv=[sys.argv[0]], verbosity=2, exit=True)
        return

    if args.voices:
        voices = [v.strip() for v in args.voices.split(",") if v.strip()]
    else:
        default = os.getenv("TTS_VOICE", "Samantha")
        voices = [default] + [v for v in _mac_voices() if v != default][:3]

    phrases = SHORT_PHRASES if args.quick else ALL_PHRASES

    # Temporarily override TTS_RATE if requested
    if args.rate != tts._RATE:
        tts._RATE = args.rate
        print(f"[tts-bench] Override: TTS_RATE → {args.rate} Hz")

    _run_benchmark(voices, args.rate, args.no_play, phrases)


if __name__ == "__main__":
    main()
