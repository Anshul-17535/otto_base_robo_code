#!/usr/bin/env python3
"""
robo_vad.py — Bob fixed-window capture or macOS silence detection

Uses a single persistent sounddevice.InputStream for the entire listen window.
Opening CoreAudio once per listen (not once per 30 ms chunk) is the only
reliable way to avoid the macOS PortAudio -50 / AUHAL error that happens when
the previous sd.rec()+sd.wait() loop is interrupted by Ctrl-C.

Silence detection:
  - Auto-calibrates ambient noise floor at first use (1 s of silence sampling)
  - Speech threshold = noise_floor × VAD_THRESHOLD_MULT (default 3.0×)
  - Overridden by VAD_ENERGY_THRESHOLD in .env (skip calibration entirely)
  - One InputStream opened per listen turn; reads 30 ms frames in a loop
  - Stops after SILENCE_AFTER_SPEECH_MS of quiet following speech
  - Never runs longer than LISTEN_DURATION seconds
  - Live level bar + per-turn summary printed to stderr
"""
from __future__ import annotations

import os
import queue
import sys
import time
import wave
from pathlib import Path

from dotenv import load_dotenv

load_dotenv(dotenv_path=Path(__file__).parent.parent / ".env", override=True)
load_dotenv(dotenv_path=Path(__file__).parent.parent.parent / "BOB_ASSISTANT" / ".env", override=False)

SAMPLE_RATE = 16_000

try:
    import numpy as np
except ImportError:
    raise ImportError(
        "numpy is required; sounddevice is needed only for Mac mic mode.\n"
        "Run:  pip install sounddevice numpy"
    )


def _log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] [vad] {msg}", file=sys.stderr, flush=True)


# ── Auto-calibration state ────────────────────────────────────────────────────
_calibrated_threshold: float | None = None


def _record_stream(duration_s: float) -> np.ndarray:
    """
    Open one InputStream, read `duration_s` seconds, close it.
    This is the macOS-safe pattern: one open/close per call, not per chunk.
    """
    import sounddevice as sd

    total_samples = int(SAMPLE_RATE * duration_s)
    buf = np.zeros(total_samples, dtype="int16")
    q: queue.Queue[np.ndarray] = queue.Queue()

    def _cb(indata, frames, time_info, status):
        q.put(indata[:, 0].copy())

    with sd.InputStream(samplerate=SAMPLE_RATE, channels=1, dtype="int16",
                        blocksize=int(SAMPLE_RATE * 0.03),   # 30 ms blocks
                        callback=_cb):
        written = 0
        deadline = time.time() + duration_s + 1.0   # 1 s grace
        while written < total_samples and time.time() < deadline:
            try:
                block = q.get(timeout=0.5)
            except queue.Empty:
                continue
            n = min(len(block), total_samples - written)
            buf[written:written + n] = block[:n]
            written += n

    return buf[:written] if written < total_samples else buf


def _calibrate() -> float:
    """
    Sample ambient audio for VAD_CAL_SECS seconds to measure the noise floor.
    Returns a threshold = 90th-percentile RMS × VAD_THRESHOLD_MULT.
    """
    mult     = float(os.getenv("VAD_THRESHOLD_MULT", "3.0"))
    cal_secs = float(os.getenv("VAD_CAL_SECS",       "1.0"))
    chunk_ms = 30
    chunk_samples = int(SAMPLE_RATE * chunk_ms / 1000)

    _log(f"calibrating noise floor ({cal_secs:.0f}s — stay quiet) …")

    audio = _record_stream(cal_secs)

    # Slice into chunks and compute RMS for each
    rms_vals: list[float] = []
    for start in range(0, len(audio) - chunk_samples + 1, chunk_samples):
        chunk = audio[start:start + chunk_samples].astype(np.float32)
        rms_vals.append(float(np.sqrt(np.mean(chunk ** 2))))

    if not rms_vals:
        _log("calibration: got no samples — using fallback threshold 200")
        return 200.0

    noise_floor = float(np.percentile(rms_vals, 90))
    threshold   = max(noise_floor * mult, 80.0)
    _log(f"noise floor = {noise_floor:.1f}  threshold = {threshold:.1f}  (mult {mult}×)")
    return threshold


def _capture_mac_until_silence(
    out_path: str,
    silence_ms: int = 800,
    max_duration_s: int = 30,
    aggressiveness: int = 3,      # unused — kept for API compatibility
) -> bool:
    """
    Record from the default mic until the user stops speaking.

    Opens one InputStream for the whole listen window — avoids the macOS
    CoreAudio -50 error that repeated sd.rec()+sd.wait() calls trigger after
    an interrupted session.

    Returns True on success, False on error.
    """
    global _calibrated_threshold
    import sounddevice as sd

    listen_duration  = int(os.getenv("LISTEN_DURATION",         "10"))
    silence_after_ms = int(os.getenv("SILENCE_AFTER_SPEECH_MS", str(silence_ms)))
    min_speech_ms    = int(os.getenv("MIN_SPEECH_MS",           "300"))

    env_threshold = os.getenv("VAD_ENERGY_THRESHOLD", "").strip()
    if env_threshold:
        energy_threshold = float(env_threshold)
    else:
        if _calibrated_threshold is None:
            try:
                _calibrated_threshold = _calibrate()
            except Exception as exc:
                _log(f"calibration failed ({exc}) — using fallback threshold 200")
                _calibrated_threshold = 200.0
        energy_threshold = _calibrated_threshold

    max_duration_s        = min(listen_duration, max_duration_s)
    chunk_ms              = 30
    chunk_samples         = int(SAMPLE_RATE * chunk_ms / 1000)
    max_chunks            = int(max_duration_s * 1000 / chunk_ms)
    silence_chunks_needed = int(silence_after_ms / chunk_ms)
    min_speech_chunks     = int(min_speech_ms / chunk_ms)

    _log(f"listening (max {max_duration_s}s, thresh {energy_threshold:.0f}, "
         f"stops {silence_after_ms}ms after speech) …")

    collected: list[np.ndarray] = []
    speech_chunks  = 0
    silence_chunks = 0
    speech_started = False
    peak_rms       = 0.0
    prev_bar_pos   = -1
    chunk_count    = 0
    done           = False

    q: queue.Queue[np.ndarray] = queue.Queue()

    def _cb(indata, frames, time_info, status):
        q.put(indata[:, 0].copy())

    try:
        stream = sd.InputStream(
            samplerate=SAMPLE_RATE,
            channels=1,
            dtype="int16",
            blocksize=chunk_samples,   # exactly one chunk per callback
            callback=_cb,
        )
        stream.start()
        _log("mic stream opened ✓")
    except sd.PortAudioError as exc:
        _log(f"PortAudio error opening stream: {exc}")
        _log("hint: check System Settings → Privacy → Microphone → allow Terminal")
        return False
    except Exception as exc:
        _log(f"stream open error: {exc}")
        return False

    deadline = time.time() + max_duration_s + 1.0

    try:
        while chunk_count < max_chunks and time.time() < deadline and not done:
            try:
                block = q.get(timeout=0.5)
            except queue.Empty:
                continue

            collected.append(block.copy())
            chunk_count += 1

            rms = float(np.sqrt(np.mean(block.astype(np.float32) ** 2)))
            if rms > peak_rms:
                peak_rms = rms

            # ── Live level bar (overwrites same terminal line) ─────────────────
            BAR_W      = 20
            bar_pos    = int(min(rms / max(energy_threshold * 2, 1) * BAR_W, BAR_W))
            thresh_pos = BAR_W // 2
            bar = "".join(
                ("█" if rms >= energy_threshold else "▒") if b < bar_pos
                else ("│" if b == thresh_pos else "░")
                for b in range(BAR_W)
            )
            state = "SPEECH" if rms >= energy_threshold else "quiet "
            if bar_pos != prev_bar_pos:
                print(f"\r[vad] [{bar}] {state}  rms={rms:5.0f}  "
                      f"peak={peak_rms:5.0f}  thresh={energy_threshold:.0f}",
                      end="", flush=True)
                prev_bar_pos = bar_pos

            if rms >= energy_threshold:
                speech_started = True
                speech_chunks += 1
                silence_chunks = 0
            else:
                if speech_started:
                    silence_chunks += 1
                    if (silence_chunks >= silence_chunks_needed
                            and speech_chunks >= min_speech_chunks):
                        print()   # newline after bar
                        done = True

    except Exception as exc:
        print()
        _log(f"capture error: {exc}")
    finally:
        stream.stop()
        stream.close()
        _log("mic stream closed")

    print()   # ensure newline after bar if loop ended at max_chunks

    if not collected:
        _log("no audio captured")
        return False

    audio      = np.concatenate(collected)
    duration_s = len(audio) / SAMPLE_RATE

    if speech_started:
        _log(f"peak_rms={peak_rms:.0f}  threshold={energy_threshold:.0f}  speech=yes ✓")
    else:
        _log(f"peak_rms={peak_rms:.0f}  threshold={energy_threshold:.0f}  "
             f"speech=NO ✗  → lower VAD_THRESHOLD_MULT or set VAD_ENERGY_THRESHOLD")

    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    with wave.open(out_path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(audio.tobytes())

    _log(f"wrote {out_path} ({duration_s:.1f}s)")
    return True


def recalibrate() -> None:
    """Force a fresh noise-floor calibration on the next capture call."""
    global _calibrated_threshold
    _calibrated_threshold = None
    _log("recalibrate: threshold will be re-measured on next capture")


def capture_until_silence(out_path: str, silence_ms: int = 800,
                          max_duration_s: int = 30, aggressiveness: int = 3,
                          source: str | None = None) -> bool:
    """Robot mode downloads one fixed 1–3 s clip; Mac mode retains live VAD.

    `source` overrides the MIC_SOURCE env for this single capture ("robot" or
    "mac"). Idle-mode wake-word listening passes "robot" so Bob hears his name
    on his own mic; conversation mode passes "mac" so it gets fast VAD that
    stops the instant the speaker finishes.
    """
    source = (source or os.getenv("MIC_SOURCE", "robot")).strip().lower()
    if source == "robot":
        from robo_robot import capture_audio
        secs = max(1, min(3, max_duration_s, int(os.getenv("ROBOT_RECORD_SECS", "3"))))
        _log(f"Bob SPH0645: recording {secs}s (fixed window; speak now)")
        return capture_audio(out_path, secs)
    if source != "mac":
        _log("MIC_SOURCE must be robot or mac")
        return False
    try:
        return _capture_mac_until_silence(out_path, silence_ms, max_duration_s, aggressiveness)
    except Exception as exc:
        _log(f"Mac capture failed: {exc}")
        return False
