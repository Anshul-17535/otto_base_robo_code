#!/usr/bin/env python3
"""
run_tests.py — Master test runner for BOB_ROBO_CONVO
=====================================================
Runs all four diagnostic tests in the correct order, merges the best-config
recommendations from each, and writes a single tests/results/OPTIMAL.env file
that can be copied directly into .env.

Order:
  1. test_llm_routing   — LLM quality + model selection (no robot needed)
  2. test_tts_pipeline  — render + transcode quality (no robot needed for --quick)
  3. test_audio_quality — upload + DMA stability (needs robot)
  4. test_latency       — screen-to-audio gap measurement (needs robot)

The merged OPTIMAL.env is the single file you set variables from in .env.

Usage
─────
    cd BOB_ROBO_CONVO
    .venv/bin/python tests/run_tests.py --host 192.168.1.15

    # Offline (no robot):
    .venv/bin/python tests/run_tests.py --no-robot

    # Quick grid for TTS (6 combos instead of full sweep):
    .venv/bin/python tests/run_tests.py --host 192.168.1.15 --quick-tts

    # Skip LLM test if Ollama is not running:
    .venv/bin/python tests/run_tests.py --host 192.168.1.15 --skip-llm

    # After running, copy optimal settings into .env:
    cat tests/results/OPTIMAL.env
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

_HERE   = Path(__file__).parent.parent          # BOB_ROBO_CONVO/
_TESTS  = Path(__file__).parent                  # BOB_ROBO_CONVO/tests/
_VENV   = _HERE / ".venv" / "bin" / "python"
PYTHON  = str(_VENV) if _VENV.exists() else sys.executable

RESULTS_DIR = _TESTS / "results"
RESULTS_DIR.mkdir(exist_ok=True)


# ── Run a sub-test ─────────────────────────────────────────────────────────────
def run_test(script: str, args: list[str], label: str) -> bool:
    cmd = [PYTHON, str(_TESTS / script)] + args
    print(f"\n{'='*60}")
    print(f"  Running: {label}")
    print(f"  CMD: {' '.join(cmd)}")
    print(f"{'='*60}")
    t0 = time.monotonic()
    result = subprocess.run(cmd, cwd=str(_HERE))
    elapsed = time.monotonic() - t0
    ok = result.returncode == 0
    print(f"\n  {'✓' if ok else '✗'} {label} finished in {elapsed:.1f}s "
          f"(exit={result.returncode})")
    return ok


# ── Merge env fragments ────────────────────────────────────────────────────────
def merge_env_files(env_files: list[Path]) -> dict[str, str]:
    """Merge multiple .env fragment files into one dict (later files win)."""
    merged: dict[str, str] = {}
    for f in env_files:
        if not f.exists():
            continue
        for line in f.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                key, _, val = line.partition("=")
                merged[key.strip()] = val.strip()
    return merged


def _read_json(path: Path) -> list | dict:
    try:
        return json.loads(path.read_text())
    except Exception:
        return {}


# ── Main ───────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(
        description="BOB_ROBO_CONVO master test runner — finds optimal config for TTS, audio, and LLM"
    )
    ap.add_argument("--host",       default=os.getenv("ROBOT_HOST", "192.168.1.15"),
                    help="ESP32 IP (default: ROBOT_HOST env or 192.168.1.15)")
    ap.add_argument("--no-robot",   action="store_true",
                    help="Skip all tests that require the physical robot")
    ap.add_argument("--quick-tts",  action="store_true",
                    help="Run the quick TTS grid (6 combos) instead of the full sweep")
    ap.add_argument("--skip-llm",   action="store_true",
                    help="Skip LLM routing test (use if Ollama is not running)")
    ap.add_argument("--skip-watsonx", action="store_true",
                    help="Skip WatsonX Stage-2 test in LLM test")
    ap.add_argument("--model",      default="",
                    help="Comma-separated Ollama model tags to test")
    args = ap.parse_args()

    robot_args = (["--no-robot"] if args.no_robot
                  else ["--host", args.host])

    print("╔══════════════════════════════════════════════════════════╗")
    print("║  BOB_ROBO_CONVO  —  Diagnostic Test Suite               ║")
    print("╚══════════════════════════════════════════════════════════╝")
    print(f"  Robot: {'OFFLINE (--no-robot)' if args.no_robot else args.host}")
    print(f"  Python: {PYTHON}")
    print(f"  Results: {RESULTS_DIR}")
    print()

    results_summary: dict[str, bool] = {}

    # ── 1. LLM routing ────────────────────────────────────────────────────────
    if not args.skip_llm:
        llm_args = []
        if args.model:
            llm_args += ["--model", args.model]
        if args.skip_watsonx:
            llm_args += ["--no-watsonx"]
        ok = run_test("test_llm_routing.py", llm_args, "LLM routing quality")
        results_summary["llm"] = ok
    else:
        print("\n[skip] LLM test (--skip-llm)")
        results_summary["llm"] = None

    # ── 2. TTS pipeline ───────────────────────────────────────────────────────
    tts_args = (robot_args +
                (["--quick"] if args.quick_tts else []))
    ok = run_test("test_tts_pipeline.py", tts_args, "TTS pipeline quality")
    results_summary["tts"] = ok

    # ── 3. Audio quality (robot only) ─────────────────────────────────────────
    ok = run_test("test_audio_quality.py", robot_args, "Audio quality / DMA stability")
    results_summary["audio"] = ok

    # ── 4. Latency ────────────────────────────────────────────────────────────
    ok = run_test("test_latency.py", robot_args, "Screen-to-audio latency")
    results_summary["latency"] = ok

    # ── Merge results into OPTIMAL.env ────────────────────────────────────────
    print(f"\n{'='*60}")
    print("  Merging results → OPTIMAL.env")
    print(f"{'='*60}")

    env_fragments = [
        RESULTS_DIR / "best_llm.env",
        RESULTS_DIR / "best_tts.env",
        RESULTS_DIR / "best_audio.env",
        RESULTS_DIR / "best_latency.env",
    ]
    merged = merge_env_files(env_fragments)

    # Sanity defaults for any key not produced by a test
    defaults = {
        "TTS_VOICE":           "Samantha",
        "TTS_RATE":            "16000",
        "TTS_HIGHPASS_HZ":     "180",
        "TTS_CHUNK_CHARS":     "90",
        "TTS_FIRST_CHUNK_CHARS": "60",
        "TTS_COMPRESS":        "1",
        "TTS_SPEECHNORM_E":    "0",
        "TTS_PEAK":            "0.8",
        "TTS_GAIN_DB":         "0",
        "OLLAMA_MODEL":        "qwen2.5:7b",
        "DISPLAY_DELAY_MS":    "0",
    }
    for k, v in defaults.items():
        merged.setdefault(k, v)

    # Write OPTIMAL.env
    lines = [
        "# ══════════════════════════════════════════════════════════════",
        "# OPTIMAL.env — generated by tests/run_tests.py",
        "# Copy the lines you want into .env",
        "# ══════════════════════════════════════════════════════════════",
        "",
        "# ── LLM ──────────────────────────────────────────────────────",
        f"OLLAMA_MODEL={merged.get('OLLAMA_MODEL', 'qwen2.5:7b')}",
        "",
        "# ── TTS Render & Transcode ───────────────────────────────────",
        f"TTS_VOICE={merged.get('TTS_VOICE', 'Samantha')}",
        f"TTS_RATE={merged.get('TTS_RATE', '16000')}",
        f"TTS_HIGHPASS_HZ={merged.get('TTS_HIGHPASS_HZ', '180')}",
        f"TTS_COMPRESS={merged.get('TTS_COMPRESS', '1')}",
        f"TTS_SPEECHNORM_E={merged.get('TTS_SPEECHNORM_E', '0')}",
        f"TTS_PEAK={merged.get('TTS_PEAK', '0.8')}",
        f"TTS_GAIN_DB={merged.get('TTS_GAIN_DB', '0')}",
        "",
        "# ── TTS Chunking (latency vs. overhead tradeoff) ─────────────",
        f"TTS_CHUNK_CHARS={merged.get('TTS_CHUNK_CHARS', '90')}",
        f"TTS_FIRST_CHUNK_CHARS={merged.get('TTS_FIRST_CHUNK_CHARS', '60')}",
        "",
        "# ── Display sync (0 = screen first; >0 = delay display to sync with audio)",
        f"# Recommended: {merged.get('DISPLAY_DELAY_MS', '0')}ms",
        f"DISPLAY_DELAY_MS={merged.get('DISPLAY_DELAY_MS', '0')}",
        "",
    ]

    optimal_path = RESULTS_DIR / "OPTIMAL.env"
    optimal_path.write_text("\n".join(lines))

    # ── Print summary ──────────────────────────────────────────────────────────
    print(f"\n{'='*60}")
    print("  TEST SUMMARY")
    print(f"{'='*60}")
    for test_name, passed in results_summary.items():
        if passed is None:
            status = "SKIP"
        elif passed:
            status = " OK "
        else:
            status = "FAIL"
        print(f"  [{status}]  {test_name}")

    print(f"\n  OPTIMAL settings → {optimal_path}")
    print()
    print("  ── OPTIMAL.env ──────────────────────────────────────────")
    for line in lines:
        if line:
            print("  " + line)
    print()
    print("  To apply: copy the lines above into BOB_ROBO_CONVO/.env")
    print("  Then restart server.py.")
    print()


if __name__ == "__main__":
    main()
