#!/usr/bin/env python3
"""
test_latency.py
===============
Measures the screen-to-audio delay — the main UX problem:
  "The message appears on the dashboard/robot screen immediately, but the
   robot speaker delivers it very late."

Root cause (from firmware analysis):
  1. Display calls (GET /eyes, /ticker, /screen/*) return in <15ms.
  2. TTS pipeline: say (~150ms) + ffmpeg (~150ms) + WiFi upload + PSRAM buffer
     fill + I2S DMA start = 500–1500ms AFTER the screen updates.
  3. The current code fires display updates INSIDE _speak_reply() via the
     `before_audio` callback — this fires after chunk 0 renders, not before.

This test measures:
  ① Display round-trip time: GET /eyes?m=0, /ticker, /screen/*
  ② TTS first-chunk latency: time from text-input to first POST /mactts
  ③ Total screen-to-audio gap (①-based zero, then ② real offset)
  ④ Optimal DISPLAY_DELAY_MS: a settable delay before firing display commands
     so the screen update arrives just before the speaker starts (sync option).

Results → tests/results/latency_results.json + best_latency.env

Usage
─────
    cd BOB_ROBO_CONVO
    .venv/bin/python tests/test_latency.py --host 192.168.1.15
    .venv/bin/python tests/test_latency.py --no-robot   # offline render-only
"""
from __future__ import annotations

import argparse
import io
import json
import os
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

_SHORT_TEXT = "Hello! I am Anvyxo."
_LONG_TEXT  = "Great question! IBM Db2 is a powerful relational database. Let me check that for you."


# ── HTTP helpers ───────────────────────────────────────────────────────────────
def _get(url: str, path: str, timeout: int = 5) -> tuple[bool, float]:
    import urllib.request
    t0 = time.monotonic()
    try:
        with urllib.request.urlopen(url + path, timeout=timeout) as r:
            r.read()
        return True, (time.monotonic() - t0) * 1000
    except Exception:
        return False, (time.monotonic() - t0) * 1000


def _post_wav(wav_bytes: bytes, url: str, timeout: int = 60) -> tuple[bool, float]:
    import urllib.request
    boundary = b"blat"
    body = (b"--" + boundary + b"\r\n"
            b'Content-Disposition: form-data; name="f"; filename="tts.wav"\r\n'
            b"Content-Type: audio/wav\r\n\r\n"
            + wav_bytes + b"\r\n--" + boundary + b"--\r\n")
    req = urllib.request.Request(
        url + "/mactts", data=body, method="POST",
        headers={"Content-Type": f"multipart/form-data; boundary={boundary.decode()}",
                 "Content-Length": str(len(body))})
    t0 = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            r.read()
        return True, (time.monotonic() - t0) * 1000
    except Exception:
        return False, (time.monotonic() - t0) * 1000


# ── Render helpers ─────────────────────────────────────────────────────────────
def _render(text: str, sr: int, hp: int, comp_af: str, peak: float = 0.8) -> tuple[bytes, float]:
    """Returns (wav_bytes, render_ms)."""
    t0 = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="bob-lat-") as d:
        tmp = Path(d)
        aiff = tmp / "s.aiff"
        subprocess.run(["/usr/bin/say", "-v",
                        os.getenv("TTS_VOICE", "Samantha"),
                        "-o", str(aiff), text],
                       check=True, capture_output=True, timeout=30)
        stages = []
        if hp > 0:
            stages.append(f"highpass=f={hp}")
        if comp_af:
            stages.extend(comp_af.split(","))
        stages.append(f"alimiter=limit={peak}:level=false")
        cmd = [FFMPEG, "-loglevel", "error", "-i", str(aiff),
               "-ar", str(sr), "-ac", "1",
               "-af", ",".join(stages) if stages else "anull",
               "-f", "s16le", "-acodec", "pcm_s16le", "pipe:1"]
        pcm = subprocess.run(cmd, check=True, capture_output=True, timeout=30).stdout
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(sr)
        wf.writeframes(pcm)
    return buf.getvalue(), (time.monotonic() - t0) * 1000


# ── Test 1: display endpoint round-trip times ─────────────────────────────────
def test_display_rtt(robot_url: str | None) -> dict:
    print("[lat] Test 1: Display endpoint RTT")
    if not robot_url:
        print("  [skip — no robot]")
        return {"test": "display_rtt", "skipped": True}

    endpoints = [
        ("/eyes?m=0",     "expression=happy"),
        ("/eyes?m=6",     "expression=normal"),
        ("/screen/idle",  "screen=idle"),
        ("/screen/speak", "screen=speak"),
        ("/ticker?text=Testing", "ticker"),
    ]
    rtts = {}
    for path, label in endpoints:
        times = []
        for _ in range(5):
            ok, ms = _get(robot_url, path)
            if ok:
                times.append(ms)
            time.sleep(0.05)
        avg = sum(times) / len(times) if times else 0
        rtts[label] = round(avg, 1)
        print(f"  {label:20s}  avg={avg:.1f}ms  ({len(times)}/5 OK)")

    max_display_ms = max(rtts.values()) if rtts else 0
    print(f"  Worst display RTT: {max_display_ms:.1f}ms")
    return {"test": "display_rtt", "rtts_ms": rtts,
            "max_display_ms": round(max_display_ms, 1)}


# ── Test 2: TTS first-chunk render time ───────────────────────────────────────
def test_render_latency() -> list[dict]:
    print("[lat] Test 2: TTS render latency (say + ffmpeg)")
    sr   = int(os.getenv("TTS_RATE", "16000"))
    hp   = int(os.getenv("TTS_HIGHPASS_HZ", "180"))
    comp = ("acompressor=threshold=0.1:ratio=6:attack=5:release=60:makeup=4,"
            "acompressor=threshold=0.125:ratio=6:attack=5:release=60:makeup=4")
    results = []
    for label, text in [("short", _SHORT_TEXT), ("long", _LONG_TEXT)]:
        times = []
        for _ in range(3):
            try:
                _, ms = _render(text, sr, hp, comp)
                times.append(ms)
            except Exception:
                pass
        avg = sum(times) / len(times) if times else 0
        print(f"  {label:8s}  len={len(text):3d}ch  avg={avg:.0f}ms  "
              f"({len(times)}/3 runs)")
        results.append({"test": "render_latency", "label": label,
                        "text_len": len(text), "avg_ms": round(avg, 1)})
    return results


# ── Test 3: end-to-end screen-to-audio gap ────────────────────────────────────
def test_screen_to_audio_gap(robot_url: str | None) -> list[dict]:
    """Simulate the actual convo loop timing exactly."""
    print("[lat] Test 3: Screen-to-audio gap simulation")
    sr   = int(os.getenv("TTS_RATE", "16000"))
    hp   = int(os.getenv("TTS_HIGHPASS_HZ", "180"))
    comp = ("acompressor=threshold=0.1:ratio=6:attack=5:release=60:makeup=4,"
            "acompressor=threshold=0.125:ratio=6:attack=5:release=60:makeup=4")

    texts = [("short", _SHORT_TEXT), ("long", _LONG_TEXT)]
    results = []

    for label, text in texts:
        # T=0: LLM has replied; fire display updates (simulate the fire-and-forget GETs)
        t_start = time.monotonic()
        if robot_url:
            _get(robot_url, "/eyes?m=0")       # expression
            _get(robot_url, "/screen/speak")    # screen mode
            _get(robot_url, "/ticker?text=Testing")
        t_display_done = time.monotonic()
        display_ms = (t_display_done - t_start) * 1000

        # Now start TTS pipeline
        try:
            wav, render_ms = _render(text, sr, hp, comp)
        except Exception as exc:
            results.append({"test": "screen_to_audio", "label": label,
                            "error": str(exc)}); continue
        t_render_done = time.monotonic()

        upload_ms = 0.0
        if robot_url:
            ok, upload_ms, = _post_wav(wav, robot_url)[:2]
        t_audio_done = time.monotonic()

        # The gap: from T=0 (screen update fired) to T=first-audio
        # = render_ms because display is fire-and-forget (async GET)
        screen_to_audio_ms = render_ms   # best case: display fires at T=0, audio at T=render_ms

        print(f"  {label:8s}: display={display_ms:.0f}ms  render={render_ms:.0f}ms  "
              f"upload={upload_ms:.0f}ms  "
              f"screen→audio gap = {screen_to_audio_ms:.0f}ms")
        results.append({
            "test":              "screen_to_audio",
            "label":             label,
            "display_ms":        round(display_ms, 1),
            "render_ms":         round(render_ms, 1),
            "upload_ms":         round(upload_ms, 1),
            "screen_to_audio_ms": round(screen_to_audio_ms, 1),
        })
    return results


# ── Test 4: display delay sync recommendation ─────────────────────────────────
def test_display_delay_recommendation(render_results: list[dict]) -> dict:
    """
    Compute the DISPLAY_DELAY_MS value that should be inserted before firing
    display updates, so the screen changes appear just as the speaker starts.

    With DISPLAY_DELAY_MS = render_ms, display fires at T=render_ms, and
    audio also starts at T≈render_ms (after upload). Net gap ≈ 0.

    The tradeoff: the user sees the text slightly later, but it's synchronised
    with voice. Set DISPLAY_DELAY_MS=0 to keep the current "screen first" behaviour.
    """
    print("[lat] Test 4: Display delay sync recommendation")
    short = next((r for r in render_results if r.get("label") == "short"), None)
    long_ = next((r for r in render_results if r.get("label") == "long"), None)
    avg_render = 0.0
    if short and long_:
        avg_render = (short.get("avg_ms", 0) + long_.get("avg_ms", 0)) / 2
    elif short:
        avg_render = short.get("avg_ms", 0)

    # Recommend delaying display by 80% of average render time.
    # The remaining 20% covers WiFi RTT for the display GET itself.
    recommended_delay = round(avg_render * 0.8)
    print(f"  avg render time: {avg_render:.0f}ms")
    print(f"  recommended DISPLAY_DELAY_MS: {recommended_delay}")
    print(f"  (set 0 to keep current 'screen first' behaviour)")
    return {
        "test": "display_delay",
        "avg_render_ms": round(avg_render, 1),
        "recommended_delay_ms": recommended_delay,
    }


# ── Main ───────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description="Screen-to-audio latency diagnostic")
    ap.add_argument("--host",     default=os.getenv("ROBOT_HOST", "192.168.1.15"))
    ap.add_argument("--no-robot", action="store_true")
    args = ap.parse_args()

    robot_url = None
    if not args.no_robot:
        h = args.host.rstrip("/")
        robot_url = h if "://" in h else f"http://{h}"
        print(f"[lat] Robot: {robot_url}")

    all_results: list = []

    rtt   = test_display_rtt(robot_url)
    all_results.append(rtt)

    rend  = test_render_latency()
    all_results.extend(rend)

    gap   = test_screen_to_audio_gap(robot_url)
    all_results.extend(gap)

    delay = test_display_delay_recommendation(rend)
    all_results.append(delay)

    out = RESULTS_DIR / "latency_results.json"
    out.write_text(json.dumps(all_results, indent=2))
    print(f"\n[lat] Results → {out}")

    # ── Write latency .env recommendations ────────────────────────────────────
    delay_ms = delay.get("recommended_delay_ms", 0)
    env_lines = [
        "# ── Latency config (generated by tests/test_latency.py) ──────────────────",
        f"# DISPLAY_DELAY_MS: delay before firing /eyes + /ticker + /screen/* so the",
        f"# screen update arrives just as the speaker starts. 0 = screen first (current).",
        f"# Recommended: {delay_ms}ms  (= ~80% of your TTS render time)",
        f"DISPLAY_DELAY_MS={delay_ms}",
    ]
    if rtt.get("max_display_ms"):
        env_lines.append(f"# Display RTT max: {rtt['max_display_ms']}ms")

    env_path = RESULTS_DIR / "best_latency.env"
    env_path.write_text("\n".join(env_lines) + "\n")
    print(f"[lat] Latency config → {env_path}")
    for line in env_lines:
        print("  " + line)


if __name__ == "__main__":
    main()
