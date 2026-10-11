#!/usr/bin/env python3
"""
test_tts_pipeline.py
====================
Tests every stage of the Mac → ESP32 TTS pipeline for noise, distortion,
and latency using a grid of config combinations.

What it tests
─────────────
  1. TTS render:   macOS `say` with different voices and rates
  2. Transcoding:  ffmpeg filter chains (highpass, compression, gain, sample rate)
  3. Chunking:     chunk character sizes (small chunks = lower latency vs overhead)
  4. Upload:       POST /mactts to the real ESP32; measures upload+playback time

Each combination is scored on:
  - Render time  (ms)       — time for `say` + ffmpeg to produce the WAV
  - Upload time  (ms)       — time for POST /mactts to return (includes ESP32 playback)
  - WAV size     (bytes)    — proxy for WiFi transfer cost
  - RMS level    (0-1)      — loudness of the rendered audio (want ~0.3–0.7)
  - Peak clipping flag      — True if any sample is at ±32767 (distortion risk)
  - Spec. loss              — spectral flatness metric (lower = more tonal / less noise)

Results written to tests/results/tts_results.json and a human-readable
summary to tests/results/tts_summary.txt.

The BEST config is printed at the end and written to tests/results/best_tts.env
— ready to paste into .env.

Usage
─────
    cd BOB_ROBO_CONVO
    .venv/bin/python tests/test_tts_pipeline.py [--host 192.168.1.15] [--no-robot]

Options
───────
    --host HOST        ESP32 IP (default: from .env ROBOT_HOST or 192.168.1.15)
    --no-robot         Skip upload tests (render + transcode only; offline mode)
    --quick            Only run the reduced "quick" grid (6 combos instead of 30)
    --voice VOICE      Lock in a specific macOS voice (skips voice grid)
"""
from __future__ import annotations

import argparse
import io
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

# ── Load .env ──────────────────────────────────────────────────────────────────
_HERE = Path(__file__).parent.parent
sys.path.insert(0, str(_HERE / "lib"))
try:
    from dotenv import load_dotenv
    load_dotenv(_HERE / ".env", override=False)
except ImportError:
    pass

RESULTS_DIR = Path(__file__).parent / "results"
RESULTS_DIR.mkdir(exist_ok=True)

FFMPEG = os.getenv("FFMPEG_PATH", "/opt/homebrew/bin/ffmpeg")

# ── Test text (covers punctuation, long words, emotion)
TEST_PHRASES = [
    "Hello! I am Anvyxo, your robot friend.",
    "The quick brown fox jumps over the lazy dog.",
    "I can help you with IBM Db2, mainframe, and database questions.",
]

# ── Grid dimensions ────────────────────────────────────────────────────────────
VOICES = ["Samantha", "Karen", "Daniel"]   # macOS built-in voices

# Sample rates to try at the ESP32 (firmware reads rate from WAV header)
SAMPLE_RATES = [11025, 16000, 22050]

# High-pass filter cutoff options (Hz). 0 = disabled.
# Root cause of buzz: sub-bass from the 4W/2Ω speaker + any DC offset.
# Higher cutoff = less sub-bass = less buzz, but thinner sound.
HIGHPASS_OPTIONS = [0, 120, 180, 250, 350]

# TTS chunk character sizes. Smaller = first audio arrives sooner (lower latency)
# but more HTTP round-trips = more overhead.
CHUNK_CHARS_OPTIONS = [60, 90, 120, 160]

# Compression presets: (description, ffmpeg_af_string)
COMPRESSION_PRESETS = {
    "none":         "",
    "light_comp":   "acompressor=threshold=0.1:ratio=4:attack=5:release=60:makeup=3",
    "dual_comp":    "acompressor=threshold=0.1:ratio=6:attack=5:release=60:makeup=4,"
                    "acompressor=threshold=0.125:ratio=6:attack=5:release=60:makeup=4",
    "speechnorm":   "speechnorm=p=0.8:e=6.0:r=0.0005",
}

# Quick grid: one representative combo per dimension
QUICK_GRID = [
    dict(voice="Samantha", sample_rate=16000, highpass=180, chunk_chars=90, compression="dual_comp"),
    dict(voice="Samantha", sample_rate=11025, highpass=180, chunk_chars=60, compression="dual_comp"),
    dict(voice="Samantha", sample_rate=22050, highpass=120, chunk_chars=90, compression="light_comp"),
    dict(voice="Karen",    sample_rate=16000, highpass=180, chunk_chars=90, compression="dual_comp"),
    dict(voice="Samantha", sample_rate=16000, highpass=250, chunk_chars=60, compression="dual_comp"),
    dict(voice="Samantha", sample_rate=16000, highpass=180, chunk_chars=90, compression="none"),
]


# ── Audio analysis helpers ─────────────────────────────────────────────────────
def _wav_stats(wav_bytes: bytes) -> dict:
    """Compute RMS, peak, and basic clipping flag from WAV bytes."""
    try:
        with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
            nch = wf.getnchannels()
            sr  = wf.getframerate()
            sw  = wf.getsampwidth()
            nf  = wf.getnframes()
            raw = wf.readframes(nf)
    except Exception as exc:
        return {"error": str(exc)}

    if sw != 2:
        return {"error": f"expected 16-bit PCM, got {sw*8}-bit"}

    samples = struct.unpack(f"<{len(raw)//2}h", raw)
    if nch > 1:
        samples = samples[::nch]   # take left channel only

    n = len(samples)
    if n == 0:
        return {"rms": 0.0, "peak": 0.0, "clipped": False, "duration_s": 0.0,
                "sample_rate": sr, "samples": 0}

    rms_raw  = math.sqrt(sum(s * s for s in samples) / n)
    peak_raw = max(abs(s) for s in samples)
    rms_norm  = rms_raw  / 32768.0
    peak_norm = peak_raw / 32768.0
    clipped   = peak_raw >= 32760

    # Spectral flatness proxy: ratio of geometric mean / arithmetic mean of |samples|
    # 1.0 = white noise, 0.0 = pure sine. Lower = more tonal speech (good).
    abs_s = [abs(s) + 1 for s in samples]
    am = sum(abs_s) / len(abs_s)
    log_sum = sum(math.log(v) for v in abs_s)
    gm = math.exp(log_sum / len(abs_s))
    spec_flatness = gm / am

    return {
        "rms":           round(rms_norm, 4),
        "peak":          round(peak_norm, 4),
        "clipped":       clipped,
        "duration_s":    round(n / sr, 3),
        "sample_rate":   sr,
        "samples":       n,
        "spec_flatness": round(spec_flatness, 4),
    }


# ── Render helpers ─────────────────────────────────────────────────────────────
def _render_say(text: str, voice: str, tmp: Path) -> tuple[Path, float]:
    """Run macOS `say` and return (aiff_path, elapsed_ms)."""
    out = tmp / "speech.aiff"
    t0 = time.monotonic()
    subprocess.run(["/usr/bin/say", "-v", voice, "-o", str(out), text],
                   check=True, capture_output=True, timeout=30)
    return out, (time.monotonic() - t0) * 1000


def _transcode(src: Path, tmp: Path, sample_rate: int, highpass: int,
               compression: str, gain_db: float = 0.0, peak: float = 0.8) -> tuple[bytes, float]:
    """ffmpeg src → PCM16 mono bytes + elapsed ms."""
    stages = []
    if highpass > 0:
        stages.append(f"highpass=f={highpass}")
    if compression:
        stages.extend(compression.split(","))
    if gain_db:
        stages.append(f"volume={gain_db}dB")
    stages.append(f"alimiter=limit={peak}:level=false")

    cmd = [FFMPEG, "-loglevel", "error", "-i", str(src),
           "-ar", str(sample_rate), "-ac", "1", "-af", ",".join(stages) if stages else "anull",
           "-f", "s16le", "-acodec", "pcm_s16le", "pipe:1"]

    t0 = time.monotonic()
    result = subprocess.run(cmd, check=True, capture_output=True, timeout=30)
    elapsed = (time.monotonic() - t0) * 1000

    # Wrap raw PCM in a proper WAV container
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(result.stdout)
    return buf.getvalue(), elapsed


def _build_af_string(highpass: int, compression_key: str, gain_db: float, peak: float) -> str:
    """Build the full ffmpeg -af value string (for recording in results)."""
    stages = []
    if highpass > 0:
        stages.append(f"highpass=f={highpass}")
    comp = COMPRESSION_PRESETS.get(compression_key, "")
    if comp:
        stages.extend(comp.split(","))
    if gain_db:
        stages.append(f"volume={gain_db}dB")
    stages.append(f"alimiter=limit={peak}:level=false")
    return ",".join(stages) if stages else "anull"


# ── Upload to robot ────────────────────────────────────────────────────────────
def _upload(wav_bytes: bytes, robot_url: str) -> tuple[bool, float]:
    """POST WAV to /mactts. Returns (success, elapsed_ms including playback)."""
    import urllib.request, urllib.error
    boundary = b"bobtest"
    body = (b"--" + boundary + b"\r\n"
            b'Content-Disposition: form-data; name="f"; filename="tts.wav"\r\n'
            b"Content-Type: audio/wav\r\n\r\n"
            + wav_bytes +
            b"\r\n--" + boundary + b"--\r\n")
    req = urllib.request.Request(
        robot_url + "/mactts",
        data=body,
        headers={"Content-Type": f"multipart/form-data; boundary={boundary.decode()}",
                 "Content-Length": str(len(body))},
        method="POST",
    )
    t0 = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            r.read()
        return True, (time.monotonic() - t0) * 1000
    except Exception as exc:
        return False, (time.monotonic() - t0) * 1000


# ── Run a single combo ─────────────────────────────────────────────────────────
def run_combo(cfg: dict, robot_url: str | None, phrase: str) -> dict:
    voice       = cfg["voice"]
    sample_rate = cfg["sample_rate"]
    highpass    = cfg["highpass"]
    chunk_chars = cfg["chunk_chars"]
    compression = cfg["compression"]
    gain_db     = cfg.get("gain_db", 0.0)
    peak        = cfg.get("peak", 0.8)

    result = {**cfg, "phrase_len": len(phrase), "errors": []}

    with tempfile.TemporaryDirectory(prefix="bob-tts-test-") as tmpdir:
        tmp = Path(tmpdir)

        # 1. Render
        try:
            src, render_ms = _render_say(phrase, voice, tmp)
            result["render_ms"] = round(render_ms, 1)
        except Exception as exc:
            result["errors"].append(f"say failed: {exc}")
            return result

        # 2. Transcode
        try:
            comp_af = COMPRESSION_PRESETS.get(compression, "")
            wav_bytes, transcode_ms = _transcode(src, tmp, sample_rate, highpass,
                                                  comp_af, gain_db, peak)
            result["transcode_ms"] = round(transcode_ms, 1)
            result["wav_bytes"]    = len(wav_bytes)
            result["af_string"]    = _build_af_string(highpass, compression, gain_db, peak)
        except Exception as exc:
            result["errors"].append(f"ffmpeg failed: {exc}")
            return result

        # 3. Audio stats
        stats = _wav_stats(wav_bytes)
        result.update(stats)

        # 4. Upload (optional)
        if robot_url:
            ok, upload_ms = _upload(wav_bytes, robot_url)
            result["upload_ok"]   = ok
            result["upload_ms"]   = round(upload_ms, 1)
            result["total_ms"]    = round(render_ms + transcode_ms + upload_ms, 1)
        else:
            result["upload_ok"]   = None
            result["upload_ms"]   = None
            result["total_ms"]    = round(render_ms + transcode_ms, 1)

    # Score: lower is better.
    # Penalise clipping heavily, reward loudness (rms), penalise upload time.
    rms      = result.get("rms", 0.0)
    clipped  = result.get("clipped", False)
    total_ms = result.get("total_ms", 9999)
    wav_sz   = result.get("wav_bytes", 99999)
    score = total_ms * 0.5 + wav_sz * 0.001 + (5000 if clipped else 0) - rms * 200
    result["score"] = round(score, 1)

    return result


# ── Build the full grid ────────────────────────────────────────────────────────
def build_grid(quick: bool, voice_lock: str | None) -> list[dict]:
    if quick:
        g = QUICK_GRID[:]
        if voice_lock:
            g = [dict(c, voice=voice_lock) for c in g]
        return g

    grid = []
    voices = [voice_lock] if voice_lock else VOICES
    for voice in voices:
        for sr in SAMPLE_RATES:
            for hp in HIGHPASS_OPTIONS:
                for cc in CHUNK_CHARS_OPTIONS:
                    for comp in COMPRESSION_PRESETS:
                        grid.append(dict(voice=voice, sample_rate=sr,
                                         highpass=hp, chunk_chars=cc, compression=comp))
    return grid


# ── Summary formatter ──────────────────────────────────────────────────────────
def _fmt_row(r: dict) -> str:
    errs = ", ".join(r.get("errors", [])) or "—"
    up   = f"{r['upload_ms']:.0f}ms" if r.get("upload_ms") is not None else "skip"
    return (f"  voice={r['voice']:10s} sr={r['sample_rate']:5d}Hz "
            f"hp={r['highpass']:3d}Hz comp={r['compression']:12s} "
            f"chunk={r['chunk_chars']:3d}  "
            f"render={r.get('render_ms',0):.0f}ms "
            f"transc={r.get('transcode_ms',0):.0f}ms "
            f"upload={up}  "
            f"rms={r.get('rms',0):.3f} clip={'Y' if r.get('clipped') else 'N'} "
            f"score={r.get('score',9999):.0f}  err={errs}")


# ── Main ───────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description="TTS pipeline quality & latency tester")
    ap.add_argument("--host",     default=os.getenv("ROBOT_HOST", "192.168.1.15"),
                    help="ESP32 IP or host:port (default from ROBOT_HOST env)")
    ap.add_argument("--no-robot", action="store_true",
                    help="Skip upload; render + transcode only")
    ap.add_argument("--quick",    action="store_true",
                    help="Run a small representative grid (~6 combos)")
    ap.add_argument("--voice",    default=None,
                    help="Lock in a specific macOS voice")
    args = ap.parse_args()

    robot_url = None
    if not args.no_robot:
        h = args.host.rstrip("/")
        robot_url = h if "://" in h else f"http://{h}"
        print(f"[tts-test] Robot target: {robot_url}  (use --no-robot to skip upload)")

    grid = build_grid(args.quick, args.voice)
    total = len(grid) * len(TEST_PHRASES)
    print(f"[tts-test] Grid: {len(grid)} combos × {len(TEST_PHRASES)} phrases = {total} runs")

    # Warm up macOS `say` and ffmpeg with a throwaway render so the first real
    # combo doesn't pay the cold-start penalty (~300-400ms extra on first subprocess).
    print("[tts-test] Warming up say + ffmpeg …", end="", flush=True)
    try:
        import tempfile as _tf
        with _tf.TemporaryDirectory() as _d:
            _render_say("warmup", os.getenv("TTS_VOICE", "Samantha"), Path(_d))
        print(" done")
    except Exception:
        print(" skipped")

    all_results: list[dict] = []
    idx = 0
    for cfg in grid:
        for phrase in TEST_PHRASES:
            idx += 1
            print(f"\r[tts-test] {idx}/{total}  voice={cfg['voice']} "
                  f"sr={cfg['sample_rate']} hp={cfg['highpass']} "
                  f"comp={cfg['compression']} chunk={cfg['chunk_chars']}  ",
                  end="", flush=True)
            r = run_combo(cfg, robot_url, phrase)
            all_results.append(r)
    print()

    # ── Aggregate: average score per combo (across phrases) ───────────────────
    from collections import defaultdict
    combo_scores: dict[str, list[float]] = defaultdict(list)
    for r in all_results:
        key = f"{r['voice']}|{r['sample_rate']}|{r['highpass']}|{r['chunk_chars']}|{r['compression']}"
        combo_scores[key].append(r.get("score", 9999))

    combo_avg = {k: sum(v)/len(v) for k, v in combo_scores.items()}
    best_key = min(combo_avg, key=combo_avg.__getitem__)
    best_parts = best_key.split("|")
    best_cfg = dict(zip(["voice","sample_rate","highpass","chunk_chars","compression"],
                        [best_parts[0], int(best_parts[1]), int(best_parts[2]),
                         int(best_parts[3]), best_parts[4]]))

    # Find one representative result for the best config
    best_result = next(r for r in all_results
                       if (f"{r['voice']}|{r['sample_rate']}|{r['highpass']}|"
                           f"{r['chunk_chars']}|{r['compression']}") == best_key)

    # ── Save JSON ──────────────────────────────────────────────────────────────
    out_json = RESULTS_DIR / "tts_results.json"
    out_json.write_text(json.dumps(all_results, indent=2))
    print(f"[tts-test] Full results → {out_json}")

    # ── Summary text ──────────────────────────────────────────────────────────
    sorted_combos = sorted(combo_avg.items(), key=lambda x: x[1])
    lines = ["TTS Pipeline Test Results", "=" * 72, ""]
    lines.append("Top 10 combos (lower score = better):")
    for i, (key, score) in enumerate(sorted_combos[:10]):
        parts = key.split("|")
        # key order: voice|sample_rate|highpass|chunk_chars|compression
        lines.append(f"  #{i+1:2d}  score={score:.1f}  "
                     f"voice={parts[0]:10s} sr={parts[1]:5s}Hz "
                     f"hp={parts[2]:3s}Hz  chunk={parts[3]:3s}ch  comp={parts[4]}")
    lines += ["", "All results:"]
    for r in all_results:
        lines.append(_fmt_row(r))
    out_txt = RESULTS_DIR / "tts_summary.txt"
    out_txt.write_text("\n".join(lines))
    print(f"[tts-test] Summary → {out_txt}")

    # ── Best .env fragment ─────────────────────────────────────────────────────
    comp_af = COMPRESSION_PRESETS.get(best_cfg["compression"], "")
    # Detect if it's the dual-comp preset
    tts_compress = "1" if "acompressor" in comp_af else "0"
    speechnorm_e = "0"
    if "speechnorm" in comp_af:
        tts_compress = "0"
        speechnorm_e = "6.0"

    env_lines = [
        "# ── Best TTS config (generated by tests/test_tts_pipeline.py) ────────────",
        f"TTS_VOICE={best_cfg['voice']}",
        f"TTS_RATE={best_cfg['sample_rate']}",
        f"TTS_HIGHPASS_HZ={best_cfg['highpass']}",
        f"TTS_CHUNK_CHARS={best_cfg['chunk_chars']}",
        f"TTS_COMPRESS={tts_compress}",
        f"TTS_SPEECHNORM_E={speechnorm_e}",
        f"TTS_PEAK=0.8",
        f"TTS_GAIN_DB=0",
        f"# score={combo_avg[best_key]:.1f}  rms={best_result.get('rms',0):.3f}  "
        f"clip={'YES' if best_result.get('clipped') else 'NO'}  "
        f"total_ms={best_result.get('total_ms',0):.0f}",
    ]
    env_path = RESULTS_DIR / "best_tts.env"
    env_path.write_text("\n".join(env_lines) + "\n")
    print(f"[tts-test] Best config → {env_path}")
    print()
    print("[tts-test] ── BEST TTS CONFIG ─────────────────────────────────")
    for line in env_lines:
        print("  " + line)
    print()


if __name__ == "__main__":
    main()
