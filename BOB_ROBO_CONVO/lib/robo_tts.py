#!/usr/bin/env python3
"""
robo_tts.py — text → speech → ESP32-S3 speaker.

Two playback modes, selected by TTS_MODE env var:

  TTS_MODE=buffer  (default)
    POST /mactts — firmware buffers the full WAV into PSRAM before playing.
    Playback is jitter-free (paced by I2S clock, not WiFi) but audio starts
    only after the entire upload lands. Use small TTS_CHUNK_CHARS (60–90) to
    keep each POST short so the first sound arrives quickly.

  TTS_MODE=stream
    POST /streamplay — firmware feeds PCM bytes straight to I2S DMA as each
    TCP chunk arrives. No PSRAM used at all. Audio starts within the first
    few TCP segments (~1–2 KB) so first-sound latency is much lower.
    Tradeoff: if WiFi hiccups mid-chunk the I2S DMA drains → audible click.
    Works best on a strong 2.4 GHz link with a short TTS_CHUNK_CHARS.

    Set in .env:
      TTS_MODE=stream
      TTS_ENDPOINT=streamplay   # (auto-set when TTS_MODE=stream)

Engines (TTS_ENGINE): "say" (macOS, default) or "piper" (local neural TTS if the
`piper` binary + a PIPER_MODEL voice are installed). Both are resampled and
loudness-shaped through ffmpeg for the small 4 W / 2 Ω speaker.
"""
from __future__ import annotations

import array
import math
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
import wave
from pathlib import Path

import requests

from robo_robot import ROBOT_BASE_URL, AUDIO_BUSY, foreground_activity

TTS_BASE_URL = os.getenv("TTS_BASE_URL", ROBOT_BASE_URL).rstrip("/")

TTS_ENGINE   = os.getenv("TTS_ENGINE", "say").strip().lower()   # say | piper
TTS_VOICE    = os.getenv("TTS_VOICE", "Samantha")
# Words-per-minute for macOS `say`. Default macOS rate is ~175 wpm which sounds
# rushed on a small speaker. 145-160 is clearer and more natural-sounding.
# Set TTS_SPEECH_RATE=0 to use the macOS default (no -r flag).
TTS_SPEECH_RATE = int(os.getenv("TTS_SPEECH_RATE", "150"))
# Silence padding (ms) appended to each chunk WAV so the I2S DMA does not drain
# to silence during the ~0.4s render gap between consecutive chunks.
TTS_PAD_MS   = int(os.getenv("TTS_PAD_MS", "300"))
# TTS_MODE selects the playback path on the ESP32:
#   buffer   → POST /mactts    (PSRAM buffered, jitter-free, higher latency)
#   stream   → POST /streamplay (direct I2S, no PSRAM, lower latency, WiFi-sensitive)
TTS_MODE     = os.getenv("TTS_MODE", "buffer").strip().lower()
# TTS_ENDPOINT overrides the endpoint explicitly; if not set, it's derived from TTS_MODE.
_ep_default  = "streamplay" if TTS_MODE == "stream" else "mactts"
TTS_ENDPOINT = "/" + os.getenv("TTS_ENDPOINT", _ep_default).strip().lstrip("/")
FFMPEG       = os.getenv("FFMPEG_PATH", "/opt/homebrew/bin/ffmpeg")
_RATE        = int(os.getenv("TTS_RATE", "16000"))
# Split long replies into phrases no longer than this many characters, so each
# upload to the robot is short.
# stream mode: smaller chunks = lower first-sound latency.
# buffer mode: smaller chunks = each POST returns sooner (pipeline gap reduced).
CHUNK_CHARS  = max(20, int(os.getenv("TTS_CHUNK_CHARS", "90")))
FIRST_CHUNK_CHARS = max(20, min(CHUNK_CHARS, int(os.getenv("TTS_FIRST_CHUNK_CHARS", str(CHUNK_CHARS)))))
_SPEECH_LOCK = threading.Lock()
# Piper (optional) — only used when TTS_ENGINE=piper.
PIPER_BIN    = os.getenv("PIPER_BIN", "piper")
PIPER_MODEL  = os.getenv("PIPER_MODEL", "")


def _log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] [tts] {msg}", file=sys.stderr, flush=True)


def write_pcm_wav(pcm: bytes, path: Path) -> float:
    """Write mono PCM16 WAV with an optional silence pad at the end.

    The pad keeps the I2S DMA fed during the ~0.4 s render gap between
    consecutive chunks so there is no audible silence break mid-sentence.
    Controlled by TTS_PAD_MS (.env). Set to 0 to disable.
    """
    if not pcm or len(pcm) % 2:
        raise ValueError("empty or incomplete PCM16 audio")
    pad_samples = int(_RATE * TTS_PAD_MS / 1000)
    pad = b"\x00" * (pad_samples * 2)   # silent PCM16 frames
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(_RATE)
        wav.writeframes(pcm + pad)
    audio_dur = len(pcm) / (2 * _RATE)   # only count real audio, not pad
    return audio_dur


def _split_phrases(text: str) -> list[str]:
    """Pack text into chunks of <= CHUNK_CHARS, breaking only when needed.

    Sentences are PACKED together up to the limit (so "Sure thing! I was made
    by engineers." stays one upload — no mid-reply gap), and only a sentence
    longer than the limit is hard-wrapped on words. The limit is deliberately
    small (~3 s of speech) so each upload finishes before the speaker's DMA
    buffer can underrun on a slow link — the cause of the "vibration" on long
    clips.
    """
    text = " ".join(text.split())
    if not text:
        return []
    sentences = re.split(r"(?<=[.!?])\s+", text)
    chunks: list[str] = []
    cur = ""
    for s in sentences:
        if len(s) > CHUNK_CHARS:                     # sentence too long — wrap it
            if cur:
                chunks.append(cur); cur = ""
            buf = ""
            for w in s.split():
                if buf and len(buf) + 1 + len(w) > CHUNK_CHARS:
                    chunks.append(buf); buf = ""
                buf = f"{buf} {w}".strip()
            if buf:
                chunks.append(buf)
        else:                                        # pack short sentences together
            if cur and len(cur) + 1 + len(s) > CHUNK_CHARS:
                chunks.append(cur); cur = ""
            cur = f"{cur} {s}".strip()
    if cur:
        chunks.append(cur)
    # A smaller opening upload reduces buffer-before-play latency. Keep the
    # remaining chunks large enough to avoid excessive phrase gaps.
    if chunks and len(chunks[0]) > FIRST_CHUNK_CHARS:
        cut = chunks[0].rfind(" ", 0, FIRST_CHUNK_CHARS + 1)
        if cut > 0:
            first = chunks.pop(0)
            chunks[:0] = [first[:cut], first[cut + 1:]]
    return chunks


def _render_to_aiff_or_wav(text: str, tmp: Path) -> Path:
    """Run the chosen TTS engine and return the path to its raw audio file."""
    if TTS_ENGINE == "piper":
        if not PIPER_MODEL:
            raise RuntimeError("TTS_ENGINE=piper but PIPER_MODEL is not set")
        out = tmp / "piper.wav"
        subprocess.run([PIPER_BIN, "--model", PIPER_MODEL, "--output_file", str(out)],
                       input=text.encode(), check=True, capture_output=True, timeout=60)
        return out
    # default: macOS `say`
    out = tmp / "speech.aiff"
    cmd = ["/usr/bin/say", "-v", TTS_VOICE, "-o", str(out)]
    if TTS_SPEECH_RATE > 0:
        cmd += ["-r", str(TTS_SPEECH_RATE)]
    cmd.append(text)
    subprocess.run(cmd, check=True, capture_output=True, timeout=60)
    return out


def _normalize_peak_pcm(pcm: bytes, target_peak: float) -> bytes:
    """Scale PCM16 so its absolute peak lands exactly on `target_peak` (0..1)."""
    samples = array.array("h")
    samples.frombytes(pcm)
    if sys.byteorder != "little":
        samples.byteswap()
    pk = max((abs(x) for x in samples), default=0)
    if pk == 0:
        return pcm
    scale = (target_peak * 32768.0) / pk
    out = array.array("h", (max(-32768, min(32767, round(x * scale))) for x in samples))
    if sys.byteorder != "little":
        out.byteswap()
    return out.tobytes()


def _lowpass_pcm(pcm: bytes, hz: float) -> bytes:
    """Low-pass PCM16 via ffmpeg. Used AFTER the soft-clip to remove the harsh
    high-frequency harmonics it adds (worst on sibilants), which the small
    speaker can't reproduce cleanly anyway. Removing them also lets the output
    peak go louder before the amp/speaker distorts (live-validated 2026-10-11:
    full-band clean ceiling ~peak 0.06, but ~0.10 with a 4.5 kHz low-pass)."""
    with tempfile.TemporaryDirectory(prefix="bob-tts-lp-") as td:
        inw = Path(td) / "in.wav"
        write_pcm_wav(pcm, inw)
        res = subprocess.run(
            [FFMPEG, "-loglevel", "error", "-i", str(inw),
             "-af", f"lowpass=f={hz}", "-ar", str(_RATE), "-ac", "1",
             "-f", "s16le", "-acodec", "pcm_s16le", "pipe:1"],
            check=True, capture_output=True, timeout=60,
        )
    return res.stdout


def _soft_clip_pcm(pcm: bytes, drive: float, target_peak: float) -> bytes:
    """Tanh soft-clip the PCM16 waveform, then normalize to `target_peak`.

    The amp/speaker output stage only plays clean up to peak ~0.06 of full
    scale ("distortion on louds" above that — a 2026-10-11 level sweep). Plain
    attenuation to that ceiling is clean but faint. This raises the AVERAGE
    level toward the ceiling without exceeding it: normalize to unit peak,
    apply y = tanh(drive * x), then scale so the peak lands exactly on
    `target_peak`. Higher drive = louder but more added soft-clip harmonics;
    the live-validated value was drive=2.0 at peak 0.06 (user A/B, +3.4 dB vs
    plain). Bit-for-bit matches tests/.../noise_isolation/loudness/drive2.wav.
    """
    samples = array.array("h")
    samples.frombytes(pcm)
    if sys.byteorder != "little":
        samples.byteswap()
    src_peak = max((abs(x) for x in samples), default=0)
    if src_peak == 0:
        return pcm
    shaped = [math.tanh(drive * (x / src_peak)) for x in samples]
    shaped_peak = max(abs(v) for v in shaped) or 1.0
    scale = (target_peak * 32768.0) / shaped_peak
    out = array.array("h", (max(-32768, min(32767, round(v * scale))) for v in shaped))
    if sys.byteorder != "little":
        out.byteswap()
    return out.tobytes()


def _to_pcm(src: Path) -> bytes:
    """Resample + loudness-shape the rendered audio to mono PCM16 @ _RATE.

    Winner from live tests: 22050 Hz, no highpass, single heavy compressor
    (ratio=8 attack=3 release=40 makeup=6) + hard limiter at 0.9.
    User-confirmed clear output uses TTS_OUTPUT_GAIN=0.25 after the limiter.
    Tune via TTS_* env vars in .env.
    """
    gain      = float(os.getenv("TTS_GAIN_DB",      "0"))
    peak      = float(os.getenv("TTS_PEAK",         "0.9"))
    output_gain = float(os.getenv("TTS_OUTPUT_GAIN", "1.0"))
    if not 0 <= output_gain <= 1:
        raise ValueError("TTS_OUTPUT_GAIN must be between 0 and 1")
    # Soft-clip loudness recovery at the amp's clean peak ceiling. When
    # TTS_SOFTCLIP_DRIVE > 0, the final level is set by the soft-clip target
    # peak (TTS_OUTPUT_PEAK) instead of TTS_OUTPUT_GAIN — see _soft_clip_pcm.
    softclip_drive = float(os.getenv("TTS_SOFTCLIP_DRIVE", "0"))
    softclip_peak  = float(os.getenv("TTS_OUTPUT_PEAK",    "0.06"))
    if softclip_drive and not 0 < softclip_peak <= 1:
        raise ValueError("TTS_OUTPUT_PEAK must be between 0 and 1")
    highpass  = float(os.getenv("TTS_HIGHPASS_HZ",  "0"))
    lowpass   = float(os.getenv("TTS_LOWPASS_HZ",   "0"))
    compress  = os.getenv("TTS_COMPRESS", "1").strip() == "1"
    # TTS_COMPRESS_PRESET selects the compressor chain:
    #   heavy (default) — ratio:8 fast attack, high makeup — loud, punchy robot voice
    #   dual            — two gentle ratio:6 stages — smoother, less pumping
    preset    = os.getenv("TTS_COMPRESS_PRESET", "heavy").strip().lower()
    stages = []
    if highpass > 0:
        stages.append(f"highpass=f={highpass}")
    if lowpass > 0:
        stages.append(f"lowpass=f={lowpass}")
    if compress:
        if preset == "dual":
            stages.append("acompressor=threshold=0.1:ratio=6:attack=5:release=60:makeup=4")
            stages.append("acompressor=threshold=0.125:ratio=6:attack=5:release=60:makeup=4")
        else:  # heavy — Test 19 winner
            stages.append("acompressor=threshold=0.1:ratio=8:attack=3:release=40:makeup=6")
    if gain:
        stages.append(f"volume={gain}dB")
    stages.append(f"alimiter=limit={peak}:level=false")
    # Attenuate AFTER compression/limiting to preserve the waveform that was
    # clear at quarter amplitude in the live A/B test. TTS_GAIN_DB acts before
    # the limiter and is not equivalent when the limiter is active. When the
    # soft-clip stage is active it sets the final level instead (normalizing
    # the limiter output), so the linear attenuation is skipped here.
    if output_gain != 1 and not softclip_drive:
        stages.append(f"volume={output_gain}")
    if os.getenv("TTS_DITHER", "0").strip() == "1":
        stages.append(f"aresample={_RATE}:dither_method=triangular_hp")
    result = subprocess.run(
        [FFMPEG, "-loglevel", "error", "-i", str(src),
         "-ar", str(_RATE), "-ac", "1", "-af", ",".join(stages),
         "-f", "s16le", "-acodec", "pcm_s16le", "pipe:1"],
        check=True, capture_output=True, timeout=60,
    )
    if softclip_drive:
        post_lowpass = float(os.getenv("TTS_POST_LOWPASS_HZ", "0"))
        if post_lowpass > 0:
            # soft-clip to unit peak, low-pass away the harsh harmonics, THEN
            # normalize to the (now higher) clean output peak. Order matters:
            # the low-pass changes peaks, so the final normalize comes last.
            shaped = _soft_clip_pcm(result.stdout, softclip_drive, 1.0)
            shaped = _lowpass_pcm(shaped, post_lowpass)
            return _normalize_peak_pcm(shaped, softclip_peak)
        return _soft_clip_pcm(result.stdout, softclip_drive, softclip_peak)
    return result.stdout


def _send_wav(wav: Path, duration: float) -> None:
    """POST one rendered WAV to the robot speaker endpoint."""
    read_timeout = max(20, duration * 8)   # slow link: upload > audio length
    with wav.open("rb") as audio:
        response = requests.post(TTS_BASE_URL + TTS_ENDPOINT,
                                 files={"f": ("tts.wav", audio, "audio/wav")},
                                 timeout=(5, read_timeout))
        response.raise_for_status()


def robot_speak(text: str, play_locally: bool = False, *, before_audio=None) -> bool:
    """Serialize whole utterances; optionally update the display during synthesis."""
    with foreground_activity(), _SPEECH_LOCK:
        return _speak(text, play_locally, before_audio=before_audio)


def _speak(text: str, play_locally: bool = False, *, before_audio=None) -> bool:
    """Speak `text` on the robot.

    Buffer mode (/mactts): the firmware plays the chunk synchronously and only
    sends HTTP 200 after playback finishes, so each POST blocks for the full
    audio duration. We therefore render and upload SERIALLY — no background
    synthesis thread. Running ffmpeg concurrently with playback causes Mac CPU /
    WiFi jitter that manifests as noise on the robot speaker.

    Stream mode (/streamplay): POST returns immediately once all bytes are sent
    (the robot plays while receiving), so pipelining is safe and used here —
    chunk N+1 is rendered in the background while chunk N is uploading.
    """
    if not text.strip():
        return False
    phrases = _split_phrases(text)
    mode_label = f"mode={TTS_MODE}"
    _log(f'▶ "{text}"  ({len(phrases)} chunk(s), {TTS_ENGINE} @ {_RATE} Hz '
         f'→ {TTS_ENDPOINT} [{mode_label}])')
    # Claim the WiFi link for audio: pause the camera/vision thread so the whole
    # bandwidth goes to the speaker stream (otherwise it underruns → breaking).
    started = time.monotonic()
    AUDIO_BUSY.set()
    ok = True
    try:
        with tempfile.TemporaryDirectory(prefix="bob-tts-") as tmpdir:
            tmp = Path(tmpdir)

            if TTS_MODE == "stream":
                # ── Pipelined path (stream mode only) ────────────────────────
                # Render N+1 in background while N is uploading — safe because
                # the POST returns immediately when bytes are sent, not when
                # playback finishes.
                import queue as _queue
                ready: "_queue.Queue" = _queue.Queue()
                cancelled = threading.Event()

                def _produce():
                    for i, phrase in enumerate(phrases):
                        if cancelled.is_set():
                            return
                        try:
                            src = _render_to_aiff_or_wav(phrase, tmp)
                            wav = tmp / f"chunk_{i}.wav"
                            dur = write_pcm_wav(_to_pcm(src), wav)
                            ready.put((wav, dur))
                        except Exception as exc:
                            ready.put(exc)
                            return
                    ready.put(None)

                prod = threading.Thread(target=_produce, daemon=True)
                prod.start()
                try:
                    if before_audio is not None:
                        before_audio()
                    idx = 0
                    while True:
                        item = ready.get()
                        if item is None:
                            break
                        if isinstance(item, Exception):
                            _log(f"render failed: {item}")
                            ok = False
                            break
                        wav, dur = item
                        idx += 1
                        if play_locally:
                            subprocess.run(["afplay", str(wav)], check=True, timeout=dur + 10)
                        try:
                            t0 = time.monotonic()
                            if idx == 1:
                                _log(f"first upload begins after {t0-started:.3f}s")
                            _send_wav(wav, dur)
                            _log(f"chunk {idx}/{len(phrases)}: {dur:.1f}s audio in "
                                 f"{time.monotonic()-t0:.1f}s via {TTS_ENDPOINT}")
                        except Exception as exc:
                            _log(f"chunk {idx} failed: {exc}")
                            ok = False
                            break
                finally:
                    cancelled.set()
                    prod.join()

            else:
                # ── Serial path (buffer mode — default) ──────────────────────
                # Render then upload each chunk sequentially. The POST blocks
                # for the full playback duration, so there is no benefit to
                # pipelining and running ffmpeg during playback causes jitter.
                if before_audio is not None:
                    before_audio()
                for i, phrase in enumerate(phrases):
                    # Render
                    try:
                        src = _render_to_aiff_or_wav(phrase, tmp)
                        wav = tmp / f"chunk_{i}.wav"
                        dur = write_pcm_wav(_to_pcm(src), wav)
                    except Exception as exc:
                        _log(f"render failed chunk {i+1}: {exc}")
                        ok = False
                        break
                    # Upload + play (POST blocks until robot finishes speaking)
                    if play_locally:
                        subprocess.run(["afplay", str(wav)], check=True, timeout=dur + 10)
                    try:
                        t0 = time.monotonic()
                        if i == 0:
                            _log(f"first upload begins after {t0-started:.3f}s")
                        _send_wav(wav, dur)
                        _log(f"chunk {i+1}/{len(phrases)}: {dur:.1f}s audio in "
                             f"{time.monotonic()-t0:.1f}s via {TTS_ENDPOINT}")
                    except Exception as exc:
                        _log(f"chunk {i+1} failed: {exc}")
                        ok = False
                        break

    except Exception as exc:
        _log(f"speech failed: {exc}")
        ok = False
    finally:
        AUDIO_BUSY.clear()
    return ok
