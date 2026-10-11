# BOB_ROBO_CONVO — Test Suite

Diagnostic tests for the two known problems:
- **Noisy / broken / distorted audio** from the ESP32 speaker
- **Screen updates immediately but audio arrives very late**

## Quick start

```bash
cd BOB_ROBO_CONVO

# Full run with robot connected (replaces 192.168.1.15 with your robot IP):
.venv/bin/python tests/run_tests.py --host 192.168.1.15

# Offline only (no robot required — skips upload tests):
.venv/bin/python tests/run_tests.py --no-robot

# Quick TTS grid (6 combos, much faster):
.venv/bin/python tests/run_tests.py --host 192.168.1.15 --quick-tts
```

After running, copy the values from `tests/results/OPTIMAL.env` into `.env` and restart `server.py`.

---

## Individual tests

| Script | What it tests | Robot needed? |
|--------|--------------|--------------|
| `test_tts_pipeline.py` | Voice, sample rate, highpass filter, compression, chunk size grid | Optional |
| `test_audio_quality.py` | Upload stability, DMA timing, WiFi throughput, sub-bass energy | Yes |
| `test_latency.py` | Display RTT, TTS render time, screen-to-audio gap, sync recommendation | Yes |
| `test_llm_routing.py` | Ollama/WatsonX JSON quality, routing accuracy, latency | No |

### Run individually

```bash
# TTS pipeline — offline quick mode
.venv/bin/python tests/test_tts_pipeline.py --no-robot --quick

# Audio quality
.venv/bin/python tests/test_audio_quality.py --host 192.168.1.15

# Latency
.venv/bin/python tests/test_latency.py --host 192.168.1.15

# LLM routing (auto-detects Ollama models)
.venv/bin/python tests/test_llm_routing.py
.venv/bin/python tests/test_llm_routing.py --model qwen2.5:3b,qwen2.5:7b
```

---

## Root causes and what the tests fix

### Problem 1 — Noise / distortion / breakup

**Root cause (from firmware `handleMacTTSData`):**
- The ESP32 buffers the entire WAV into PSRAM before playing. If the WAV has sub-bass energy (below ~180 Hz), the 4W/2Ω MAX98357A cone buzzes.
- If WiFi is contended and upload stalls, the I2S DMA buffer (8 × 64 samples = 32ms) drains → click/pop.
- Sample rate mismatch (Mac renders at one rate, firmware I2S clock set to another) → pitch-shifted or choppy audio.

**What `test_audio_quality.py` finds:**
- Optimal sample rate for your WiFi link (lowest upload overhead with stable DMA).
- Optimal highpass cutoff (where lf_ratio drops below 0.5 → buzz disappears).
- WiFi throughput baseline (KB/s) to your robot.

**What `test_tts_pipeline.py` finds:**
- Optimal `TTS_CHUNK_CHARS` — smaller chunks upload faster individually, reducing the chance of a single long stall causing a glitch.
- Whether `dual_comp`, `light_comp`, or `speechnorm` produces the cleanest audio without clipping.

### Problem 2 — Screen shows text, audio arrives late

**Root cause:**
- Display calls (GET `/eyes`, `/ticker`, `/screen/*`) return in <15ms.
- TTS pipeline: `say` (~150ms) + ffmpeg (~150ms) + WiFi upload = 500–1500ms.
- So screen updates at T=0ms, audio starts at T≈500–1500ms.

**What `test_latency.py` measures:**
- Exact display endpoint RTT for your robot.
- TTS render time for short and long phrases.
- The actual screen-to-audio gap in your environment.
- A recommended `DISPLAY_DELAY_MS` value.

**New `DISPLAY_DELAY_MS` env var (added to `server.py`):**
- Set to `0` (default) = current "screen first" behaviour.
- Set to e.g. `280` = display fires 280ms into the TTS render, so screen and audio arrive together.
- Value comes from `tests/results/OPTIMAL.env` after running the tests.

---

## Output files

All results are written to `tests/results/`:

| File | Contents |
|------|---------|
| `tts_results.json` | Full TTS grid results (all combos × all phrases) |
| `tts_summary.txt` | Human-readable ranked summary |
| `best_tts.env` | Best TTS config fragment |
| `audio_quality_results.json` | Sample rate, chunk, WiFi, sub-bass test results |
| `best_audio.env` | Best audio config fragment |
| `latency_results.json` | Display RTT, render time, screen-to-audio gap |
| `best_latency.env` | Best latency config fragment |
| `llm_results.json` | Full LLM routing test results |
| `best_llm.env` | Best model selection |
| **`OPTIMAL.env`** | **Merged best config — copy into `.env`** |
