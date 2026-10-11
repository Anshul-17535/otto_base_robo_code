#!/usr/bin/env python3
"""
test_audio_quality.py
=====================
Targets the two specific problems found with the ESP32-S3 (MAX98357A) speaker:

PROBLEM 1 — Noise / Distortion / Breakup
  Root cause (from firmware analysis):
    • handleMacTTSData() buffers the entire WAV into PSRAM before playing.
      The MAX98357A is a Class-D amp; it amplifies everything including HF
      noise from the ffmpeg pipeline. If the DMA buffer underruns even once
      during upload, you get a click or pop.
    • The I2S driver uses dma_buf_count=8, dma_buf_len=64 (512 samples = 32 ms).
      If the WiFi TCP segment arrives faster than I2S drains it, the samples
      pile up fine; if it arrives slower, the DMA FIFO drains to zero → pop.
    • WAV sample rate mismatch: firmware expects 16 kHz default. If the WAV
      header says a different rate and firmware doesn't read it correctly,
      the I2S clock is wrong → sped-up / slowed-down / distorted playback.

PROBLEM 2 — Screen updates immediately, audio arrives late
  Root cause: display calls are fire-and-forget GET (<15ms). TTS pipeline
  is: say (~150ms) + ffmpeg (~150ms) + WiFi upload (size/bandwidth) = 500-1500ms.
  The `before_audio` callback fires AFTER the first chunk renders but BEFORE
  upload — so there's still a render+transcode gap.

What this test measures
───────────────────────
  • For each sample rate: sends a pre-synthesised WAV and records upload time
    and whether the robot reported success (HTTP 200).
  • Finds the optimal chunk size: smaller chunks reduce first-audio latency
    but increase HTTP overhead. Measures the round-trip time per chunk.
  • WiFi throughput: measures MB/s upload rate to the robot.
  • DMA stability: sends a long clip and measures if the POST returns in
    duration ± 20% (a large overrun indicates WiFi contention).

Results → tests/results/audio_quality_results.json + best_audio.env

Usage
─────
    cd BOB_ROBO_CONVO
    .venv/bin/python tests/test_audio_quality.py --host 192.168.1.15
    .venv/bin/python tests/test_audio_quality.py --no-robot   # offline only
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

# ── Synthesise reference audio ─────────────────────────────────────────────────
_TEST_TEXT = "Hello I am Anvyxo your friendly robot. I can answer your questions."
_LONG_TEXT = (
    "This is a longer test sentence for the robot speaker. "
    "I want to check if the audio plays smoothly without any breakup or distortion. "
    "The quick brown fox jumps over the lazy dog. "
    "IBM Db2 is a relational database management system."
)


def _synth_wav(text: str, sample_rate: int, highpass: int,
               compression_af: str, peak: float = 0.8) -> bytes:
    """Render text → WAV bytes using macOS say + ffmpeg."""
    with tempfile.TemporaryDirectory(prefix="bob-aq-") as tmpdir:
        tmp = Path(tmpdir)
        aiff = tmp / "s.aiff"
        subprocess.run(["/usr/bin/say", "-v",
                        os.getenv("TTS_VOICE", "Samantha"),
                        "-o", str(aiff), text],
                       check=True, capture_output=True, timeout=30)
        stages = []
        if highpass > 0:
            stages.append(f"highpass=f={highpass}")
        if compression_af:
            stages.extend(compression_af.split(","))
        stages.append(f"alimiter=limit={peak}:level=false")
        cmd = [FFMPEG, "-loglevel", "error", "-i", str(aiff),
               "-ar", str(sample_rate), "-ac", "1",
               "-af", ",".join(stages) if stages else "anull",
               "-f", "s16le", "-acodec", "pcm_s16le", "pipe:1"]
        pcm = subprocess.run(cmd, check=True, capture_output=True, timeout=30).stdout
        buf = io.BytesIO()
        with wave.open(buf, "wb") as wf:
            wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(sample_rate)
            wf.writeframes(pcm)
        return buf.getvalue()


# ── Upload helpers ─────────────────────────────────────────────────────────────
def _post_wav(wav_bytes: bytes, robot_url: str, timeout: int = 60) -> tuple[bool, float, int]:
    """POST a WAV to /mactts. Returns (ok, elapsed_ms, http_status)."""
    import urllib.request
    boundary = b"bobaq"
    body = (b"--" + boundary + b"\r\n"
            b'Content-Disposition: form-data; name="f"; filename="tts.wav"\r\n'
            b"Content-Type: audio/wav\r\n\r\n"
            + wav_bytes
            + b"\r\n--" + boundary + b"--\r\n")
    req = urllib.request.Request(
        robot_url + "/mactts", data=body, method="POST",
        headers={"Content-Type": f"multipart/form-data; boundary={boundary.decode()}",
                 "Content-Length": str(len(body))})
    t0 = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            status = r.status; r.read()
        return True, (time.monotonic() - t0) * 1000, status
    except Exception as exc:
        return False, (time.monotonic() - t0) * 1000, 0


def _wav_duration(wav_bytes: bytes) -> float:
    try:
        with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
            return wf.getnframes() / wf.getframerate()
    except Exception:
        return 0.0


# ── Test 1: sample rate stability ─────────────────────────────────────────────
def test_sample_rates(robot_url: str | None) -> list[dict]:
    """Send a short phrase at each sample rate; measure round-trip timing."""
    print("[aq] Test 1: Sample rate vs. upload time & stability")
    sample_rates = [8000, 11025, 16000, 22050]
    highpass     = int(os.getenv("TTS_HIGHPASS_HZ", "180"))
    comp         = ("acompressor=threshold=0.1:ratio=6:attack=5:release=60:makeup=4,"
                    "acompressor=threshold=0.125:ratio=6:attack=5:release=60:makeup=4")
    results = []
    for sr in sample_rates:
        try:
            wav = _synth_wav(_TEST_TEXT, sr, highpass, comp)
        except Exception as exc:
            results.append({"test": "sample_rate", "sample_rate": sr, "error": str(exc)})
            continue
        dur = _wav_duration(wav)
        size_kb = len(wav) / 1024
        r = {"test": "sample_rate", "sample_rate": sr,
             "duration_s": round(dur, 2), "size_kb": round(size_kb, 1)}
        if robot_url:
            ok, elapsed, status = _post_wav(wav, robot_url)
            r["upload_ok"]     = ok
            r["http_status"]   = status
            r["upload_ms"]     = round(elapsed, 1)
            r["upload_kb_s"]   = round(size_kb / (elapsed / 1000), 1) if elapsed else 0
            # DMA stability: POST should return in roughly duration + upload_overhead
            # overhead expected ~50–300ms per chunk on LAN. Flag if wildly off.
            overhead_ms = elapsed - dur * 1000
            r["overhead_ms"]   = round(overhead_ms, 1)
            r["dma_stable"]    = overhead_ms < dur * 500  # <50% of audio length overhead
            print(f"  sr={sr:5d}Hz  {size_kb:.1f}KB  dur={dur:.2f}s  "
                  f"upload={elapsed:.0f}ms  overhead={overhead_ms:.0f}ms  "
                  f"{'OK' if ok else 'FAIL'}")
        else:
            print(f"  sr={sr:5d}Hz  {size_kb:.1f}KB  dur={dur:.2f}s  [no robot]")
        results.append(r)
    return results


# ── Test 2: chunk size vs latency ──────────────────────────────────────────────
def test_chunk_latency(robot_url: str | None) -> list[dict]:
    """Measure how chunk size affects time-to-first-audio."""
    print("[aq] Test 2: Chunk size vs. first-audio latency")
    sr       = int(os.getenv("TTS_RATE", "16000"))
    highpass = int(os.getenv("TTS_HIGHPASS_HZ", "180"))
    comp     = ("acompressor=threshold=0.1:ratio=6:attack=5:release=60:makeup=4,"
                "acompressor=threshold=0.125:ratio=6:attack=5:release=60:makeup=4")
    # Chunk sizes in characters
    chunk_sizes = [40, 60, 90, 120, 160, 200]
    results = []
    # Import the actual splitter
    try:
        import robo_tts as tts
    except Exception:
        tts = None

    for cc in chunk_sizes:
        r = {"test": "chunk_latency", "chunk_chars": cc}
        if tts:
            # Temporarily override the chunk setting
            orig_cc = tts.CHUNK_CHARS
            orig_fcc = tts.FIRST_CHUNK_CHARS
            tts.CHUNK_CHARS = cc
            tts.FIRST_CHUNK_CHARS = min(cc, 60)
            chunks = tts._split_phrases(_LONG_TEXT)
            tts.CHUNK_CHARS = orig_cc
            tts.FIRST_CHUNK_CHARS = orig_fcc
            r["num_chunks"]   = len(chunks)
            r["first_chunk_chars"] = len(chunks[0]) if chunks else 0
            # Synthesise just the first chunk to measure first-audio latency
            first_text = chunks[0] if chunks else _TEST_TEXT[:cc]
        else:
            # Fallback: just split by char count
            words = _LONG_TEXT.split()
            buf, first_text = "", ""
            for w in words:
                if buf and len(buf) + 1 + len(w) > cc:
                    first_text = buf; buf = w; break
                buf = (buf + " " + w).strip()
            if not first_text:
                first_text = buf
            r["num_chunks"] = math.ceil(len(_LONG_TEXT) / cc)
            r["first_chunk_chars"] = len(first_text)

        try:
            wav = _synth_wav(first_text, sr, highpass, comp)
            r["first_chunk_ms_render"] = round(_synth_wav_timed(first_text, sr, highpass, comp), 1)
        except Exception as exc:
            r["error"] = str(exc); results.append(r); continue

        if robot_url:
            ok, upload_ms, _ = _post_wav(wav, robot_url)
            r["first_audio_ms"] = round(r.get("first_chunk_ms_render", 0) + upload_ms, 1)
            r["upload_ms"]      = round(upload_ms, 1)
            r["upload_ok"]      = ok
            print(f"  chunk={cc:3d}ch  chunks={r.get('num_chunks',0)}  "
                  f"first_chunk={r['first_chunk_chars']}ch  "
                  f"render={r.get('first_chunk_ms_render',0):.0f}ms  "
                  f"upload={upload_ms:.0f}ms  total={r['first_audio_ms']:.0f}ms")
        else:
            r["first_audio_ms"] = r.get("first_chunk_ms_render", 0)
            print(f"  chunk={cc:3d}ch  first_audio~={r['first_audio_ms']:.0f}ms [no upload]")
        results.append(r)
    return results


def _synth_wav_timed(text: str, sr: int, highpass: int, comp: str) -> float:
    t0 = time.monotonic()
    _synth_wav(text, sr, highpass, comp)
    return (time.monotonic() - t0) * 1000


# ── Test 3: WiFi throughput ────────────────────────────────────────────────────
def test_wifi_throughput(robot_url: str | None) -> dict:
    """Upload a known-size payload 5 times and measure average MB/s."""
    print("[aq] Test 3: WiFi throughput to robot /mactts")
    if not robot_url:
        print("  [skip — no robot]")
        return {"test": "wifi_throughput", "skipped": True}
    sr  = 16000
    # 2 seconds of silence WAV ~= 64 KB
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(sr)
        wf.writeframes(b"\x00\x00" * (sr * 2))
    silence_wav = buf.getvalue()
    size_kb = len(silence_wav) / 1024
    times = []
    for i in range(5):
        ok, ms, _ = _post_wav(silence_wav, robot_url, timeout=30)
        if ok:
            times.append(ms)
            print(f"  run {i+1}/5: {ms:.0f}ms  {size_kb/(ms/1000):.0f} KB/s")
        else:
            print(f"  run {i+1}/5: FAILED")
    if not times:
        return {"test": "wifi_throughput", "error": "all uploads failed"}
    avg_ms  = sum(times) / len(times)
    avg_kbs = size_kb / (avg_ms / 1000)
    return {"test": "wifi_throughput",
            "size_kb": round(size_kb, 1),
            "avg_ms":  round(avg_ms, 1),
            "avg_kb_s": round(avg_kbs, 1),
            "samples": len(times)}


# ── Test 4: DC offset / low-freq energy ───────────────────────────────────────
def _iir_lowpass_rms(samples: tuple, sr: int, cutoff_hz: float) -> float:
    """Single-pole IIR lowpass filter to isolate sub-bass energy.

    RC = 1 / (2π * cutoff_hz). Coefficient α = dt / (RC + dt) where dt = 1/sr.
    This correctly attenuates everything above cutoff_hz by ~20 dB/decade,
    unlike a moving-average which only attenuates at multiples of its window.
    """
    import math as _math
    rc = 1.0 / (2.0 * _math.pi * cutoff_hz)
    dt = 1.0 / sr
    alpha = dt / (rc + dt)   # ≈ 0.0056 for 14 Hz cutoff at 16 kHz; ≈ 0.045 for 120 Hz
    y = 0.0
    sq_sum = 0.0
    n = len(samples)
    for s in samples:
        y = alpha * s + (1.0 - alpha) * y
        sq_sum += y * y
    return _math.sqrt(sq_sum / n) / 32768.0 if n else 0.0


def test_lowfreq_energy(robot_url: str | None) -> list[dict]:
    """Compare sub-bass energy (<120 Hz) with/without highpass filter.

    Uses a single-pole IIR lowpass at 120 Hz — properly isolates sub-bass
    unlike the old 5-tap moving-average which was too broad and showed
    lf_ratio ~0.93 for ALL highpass settings (no useful signal).
    High sub-bass energy (lf_ratio > 0.15) → speaker buzz on 4W/2Ω cone.
    """
    print("[aq] Test 4: Sub-bass energy — IIR lowpass at 120 Hz (root cause of buzz)")
    text = _TEST_TEXT
    sr   = 16000
    comp = ("acompressor=threshold=0.1:ratio=6:attack=5:release=60:makeup=4,"
            "acompressor=threshold=0.125:ratio=6:attack=5:release=60:makeup=4")
    results = []
    highpass_options = [0, 80, 120, 180, 250, 350]
    for hp in highpass_options:
        try:
            wav = _synth_wav(text, sr, hp, comp)
        except Exception as exc:
            results.append({"test": "lowfreq", "highpass": hp, "error": str(exc)})
            continue
        with wave.open(io.BytesIO(wav), "rb") as wf:
            raw = wf.readframes(wf.getnframes())
        samples = struct.unpack(f"<{len(raw)//2}h", raw)
        n = len(samples)
        # IIR lowpass at 120 Hz — properly isolates sub-bass
        lf_rms    = _iir_lowpass_rms(samples, sr, cutoff_hz=120.0)
        total_rms = math.sqrt(sum(s * s for s in samples) / n) / 32768.0 if n else 0.0
        lf_ratio  = lf_rms / total_rms if total_rms else 0
        buzzy = lf_ratio > 0.15
        print(f"  hp={hp:3d}Hz  total_rms={total_rms:.4f}  lf_rms(120Hz)={lf_rms:.5f}  "
              f"lf_ratio={lf_ratio:.4f}  {'← sub-bass present (buzz risk)' if buzzy else '✓ clean'}")
        results.append({"test": "lowfreq", "highpass": hp,
                        "total_rms": round(total_rms, 4),
                        "lf_rms": round(lf_rms, 5),
                        "lf_ratio": round(lf_ratio, 4)})
    return results


# ── Main ───────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description="Audio quality diagnostic for BOB ESP32 speaker")
    ap.add_argument("--host",     default=os.getenv("ROBOT_HOST", "192.168.1.15"))
    ap.add_argument("--no-robot", action="store_true")
    args = ap.parse_args()

    robot_url = None
    if not args.no_robot:
        h = args.host.rstrip("/")
        robot_url = h if "://" in h else f"http://{h}"
        print(f"[aq] Robot: {robot_url}")

    all_results: list = []
    all_results.extend(test_sample_rates(robot_url))
    all_results.extend(test_chunk_latency(robot_url))
    all_results.append(test_wifi_throughput(robot_url))
    all_results.extend(test_lowfreq_energy(robot_url))

    out = RESULTS_DIR / "audio_quality_results.json"
    out.write_text(json.dumps(all_results, indent=2))
    print(f"\n[aq] Results → {out}")

    # ── Derive best audio .env recommendations ────────────────────────────────
    # Best sample rate: lowest overhead_ms that is still DMA stable
    sr_results = [r for r in all_results if r.get("test") == "sample_rate" and r.get("dma_stable")]
    best_sr = 16000
    if sr_results:
        best_sr_row = min(sr_results, key=lambda r: r.get("overhead_ms", 9999))
        best_sr = best_sr_row["sample_rate"]

    # Best highpass: pick the highest cutoff where lf_ratio is still dropping
    # meaningfully (pick the value with the lowest lf_ratio — most sub-bass removed).
    # The IIR 120 Hz metric shows the fraction of signal energy below 120 Hz;
    # for macOS `say` speech this starts around 0.42 (no filter) and drops as
    # more sub-bass is cut. We want the lowest achievable lf_ratio within the
    # highpass_options list.
    lf_results = sorted([r for r in all_results if r.get("test") == "lowfreq"],
                        key=lambda r: r.get("highpass", 0))
    best_hp = 180
    if lf_results:
        # Pick the first option where lf_ratio has dropped by at least 15% from baseline
        baseline = lf_results[0].get("lf_ratio", 1.0)
        for r in lf_results[1:]:
            drop = baseline - r.get("lf_ratio", baseline)
            if drop >= baseline * 0.15:   # 15% relative reduction
                best_hp = r["highpass"]

    # Best chunk chars: lowest first_audio_ms
    chunk_results = [r for r in all_results if r.get("test") == "chunk_latency"
                     and not r.get("error")]
    best_cc = 90
    if chunk_results:
        best_cc = min(chunk_results, key=lambda r: r.get("first_audio_ms", 9999))["chunk_chars"]

    env_lines = [
        "# ── Best audio config (generated by tests/test_audio_quality.py) ─────────",
        f"TTS_RATE={best_sr}",
        f"TTS_HIGHPASS_HZ={best_hp}",
        f"TTS_CHUNK_CHARS={best_cc}",
        f"TTS_FIRST_CHUNK_CHARS={min(best_cc, 60)}",
    ]
    env_path = RESULTS_DIR / "best_audio.env"
    env_path.write_text("\n".join(env_lines) + "\n")
    print(f"[aq] Best audio config → {env_path}")
    for line in env_lines:
        print("  " + line)


if __name__ == "__main__":
    main()
