#!/usr/bin/env python3
"""
test_llm_models.py — Multi-LLM / multi-model latency benchmark for BOB_ROBO_CONVO.

Runs a fixed set of test prompts against:
  • Every Ollama model tag listed in BENCH_OLLAMA_MODELS (default: all pulled models)
  • Watsonx (if LLM_API_KEY is set) with every model in BENCH_WATSONX_MODELS

Prints a summary table of latency (ms), valid JSON %, and expression diversity.

Usage:
    cd BOB_ROBO_CONVO
    .venv/bin/python tests/test_llm_models.py                  # benchmark all
    .venv/bin/python tests/test_llm_models.py --ollama-only    # skip Watsonx
    .venv/bin/python tests/test_llm_models.py --quick          # 3 prompts only

Environment:
    BENCH_OLLAMA_MODELS  comma-sep list of Ollama tags to test (default: auto)
    BENCH_WATSONX_MODELS comma-sep list of WatsonX model IDs to test
    OLLAMA_BASE_URL      (same as robo_llm.py)
    LLM_API_KEY / LLM_BASE_URL / PROJECT_ID   (same as robo_llm.py)
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import requests
from dotenv import load_dotenv

load_dotenv(dotenv_path=Path(__file__).resolve().parents[1] / ".env", override=True)
load_dotenv(dotenv_path=Path(__file__).resolve().parents[1].parent / "BOB_ASSISTANT" / ".env",
            override=False)

import robo_llm as _llm

# ── Benchmark prompts ──────────────────────────────────────────────────────────
ALL_PROMPTS = [
    # General conversation
    ("hello",           "Hi, how are you doing today?"),
    ("weather",         "What's the weather like on Jupiter?"),
    ("joke",            "Tell me a short joke!"),
    ("dance",           "Can you do a little dance for me?"),
    ("photo",           "Take a selfie!"),
    ("goodbye",         "Goodbye for now."),
    # Factual / edge cases
    ("capital",         "What is the capital of Australia?"),
    ("math",            "What is 7 times 8?"),
    ("long",            "Tell me something interesting about quantum computing."),
    # Robot personality
    ("emotion_sad",     "I lost my keys and I'm really upset."),
    ("emotion_happy",   "I just got promoted!"),
    ("move_request",    "Do a robot dance!"),
    # DB2 routing
    ("db2",             "How do I run RUNSTATS on a Db2 for z/OS table?"),
    ("sql",             "What is a buffer pool in Db2?"),
]

QUICK_PROMPTS = ALL_PROMPTS[:3]

VALID_EXPR  = {"happy", "sad", "angry", "surprised", "sleepy", "love", "normal"}
REQUIRED_KEYS = {"reply_text", "expression", "move_cmd", "show_text",
                 "should_stop", "need_outside_help"}

# Sample moves (simulates what server.py loads from /movelist)
_SAMPLE_MOVES = [
    "Happy wiggle", "Excited hop", "Celebrate", "Bounce (sync)", "Wiggle",
    "Leg wave", "Double take", "Curious", "Curious lean", "Peek",
    "Cha-cha", "Moonwalk slide", "Foot dance", "Robot",
]
_llm.set_available_moves(_SAMPLE_MOVES)


def _get_ollama_models(base_url: str) -> list[str]:
    """List all locally pulled Ollama model tags."""
    try:
        r = requests.get(f"{base_url}/api/tags", timeout=5)
        r.raise_for_status()
        return [m["name"] for m in r.json().get("models", [])]
    except Exception as exc:
        print(f"  [warn] Cannot list Ollama models: {exc}", file=sys.stderr)
        return []


def _bench_one_ollama(model: str, base_url: str, prompts: list) -> dict:
    """Run all prompts against one Ollama model and return stats."""
    results = []
    for tag, text in prompts:
        msgs = [{"role": "user", "content": text}]
        t0 = time.perf_counter()
        try:
            r = requests.post(
                f"{base_url}/api/chat",
                json={
                    "model":    model,
                    "messages": [{"role": "system", "content": _llm._system()}] + msgs,
                    "stream":   False,
                    "format":   "json",
                    "options":  {"temperature": 0.4, "num_predict": 200},
                },
                timeout=30,
            )
            r.raise_for_status()
            raw = r.json().get("message", {}).get("content", "").strip()
            latency_ms = (time.perf_counter() - t0) * 1000
            parsed = _llm._parse(raw, text)
            results.append({
                "tag": tag, "latency_ms": latency_ms,
                "valid_json": True,
                "reply": parsed.get("reply_text", "")[:60],
                "expr": parsed.get("expression", "normal"),
                "move": parsed.get("move_cmd"),
                "stop": parsed.get("should_stop"),
                "db2":  parsed.get("need_outside_help"),
                "raw_len": len(raw),
            })
        except Exception as exc:
            latency_ms = (time.perf_counter() - t0) * 1000
            results.append({
                "tag": tag, "latency_ms": latency_ms,
                "valid_json": False,
                "reply": f"ERROR: {exc}",
                "expr": "—", "move": None,
                "stop": False, "db2": False, "raw_len": 0,
            })
        _print_row(model[:24], tag, results[-1])
    return _aggregate(results)


def _bench_one_watsonx(model_id: str, prompts: list) -> dict:
    """Run all prompts against one Watsonx model and return stats."""
    results = []
    for tag, text in prompts:
        msgs = [{"role": "user", "content": text}]
        t0 = time.perf_counter()
        try:
            token = _llm._get_iam_token()
            r = requests.post(
                f"{_llm.LLM_BASE_URL}/ml/v1/text/chat?version=2023-05-29",
                headers={"Authorization": f"Bearer {token}",
                         "Content-Type": "application/json"},
                json={
                    "model_id":   model_id,
                    "project_id": _llm.PROJECT_ID,
                    "messages":   [{"role": "system", "content": _llm._system()}] + msgs,
                    "parameters": {"max_new_tokens": 200, "temperature": 0.4},
                },
                timeout=40,
            )
            r.raise_for_status()
            d = r.json()
            choices = d.get("choices") or []
            raw = choices[0].get("message", {}).get("content", "").strip() if choices else ""
            if not raw:
                results_d = d.get("results") or []
                raw = results_d[0].get("generated_text", "").strip() if results_d else ""
            latency_ms = (time.perf_counter() - t0) * 1000
            parsed = _llm._parse(raw, text)
            results.append({
                "tag": tag, "latency_ms": latency_ms,
                "valid_json": True,
                "reply": parsed.get("reply_text", "")[:60],
                "expr": parsed.get("expression", "normal"),
                "move": parsed.get("move_cmd"),
                "stop": parsed.get("should_stop"),
                "db2":  parsed.get("need_outside_help"),
                "raw_len": len(raw),
            })
        except Exception as exc:
            latency_ms = (time.perf_counter() - t0) * 1000
            results.append({
                "tag": tag, "latency_ms": latency_ms,
                "valid_json": False,
                "reply": f"ERROR: {exc}",
                "expr": "—", "move": None,
                "stop": False, "db2": False, "raw_len": 0,
            })
        _print_row(model_id[:24], tag, results[-1])
    return _aggregate(results)


def _aggregate(results: list[dict]) -> dict:
    latencies = [r["latency_ms"] for r in results if r["valid_json"]]
    valid = [r for r in results if r["valid_json"]]
    exprs = {r["expr"] for r in valid if r["expr"] in VALID_EXPR}
    # Check routing accuracy: goodbye → should_stop, db2 tags → need_outside_help
    stop_ok = all(r["stop"] for r in results if r["tag"] == "goodbye")
    db2_ok  = all(r["db2"]  for r in results if r["tag"] in ("db2", "sql"))
    return {
        "count":      len(results),
        "valid_pct":  100 * len(valid) / max(len(results), 1),
        "p50_ms":     sorted(latencies)[len(latencies) // 2] if latencies else 0,
        "p95_ms":     sorted(latencies)[int(len(latencies) * 0.95)] if latencies else 0,
        "min_ms":     min(latencies) if latencies else 0,
        "max_ms":     max(latencies) if latencies else 0,
        "expr_count": len(exprs),
        "stop_ok":    stop_ok,
        "db2_ok":     db2_ok,
        "total_ms":   sum(latencies),
    }


def _print_row(model: str, tag: str, r: dict) -> None:
    ok_mark = "✓" if r["valid_json"] else "✗"
    db2_mark = "[db2]" if r["db2"] else ""
    stop_mark = "[stop]" if r["stop"] else ""
    move_mark = f"[{r['move'][:10]}]" if r["move"] else ""
    print(f"  {ok_mark} {r['latency_ms']:6.0f}ms  [{tag:<12}]  {r['reply'][:50]!r:50s}"
          f"  {r['expr']:<9}  {db2_mark}{stop_mark}{move_mark}")


def _print_summary(backend: str, model: str, stats: dict) -> None:
    print(f"\n  {'Backend':<10} {'Model':<36} {'p50(ms)':>8} {'p95(ms)':>8} "
          f"{'valid%':>7} {'exprs':>6} {'stop':>5} {'db2':>4}")
    print(f"  {'-'*10} {'-'*36} {'-'*8} {'-'*8} {'-'*7} {'-'*6} {'-'*5} {'-'*4}")
    stop_s  = "✓" if stats["stop_ok"] else "✗"
    db2_s   = "✓" if stats["db2_ok"] else "✗"
    print(f"  {backend:<10} {model:<36} {stats['p50_ms']:>8.0f} {stats['p95_ms']:>8.0f} "
          f"  {stats['valid_pct']:>5.0f}% {stats['expr_count']:>6} {stop_s:>5} {db2_s:>4}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ollama-only", action="store_true")
    ap.add_argument("--watsonx-only", action="store_true")
    ap.add_argument("--quick", action="store_true", help="Only run first 3 prompts")
    ap.add_argument("--save", metavar="FILE", help="Save JSON results to file")
    args = ap.parse_args()

    prompts = QUICK_PROMPTS if args.quick else ALL_PROMPTS
    base_url = _llm.OLLAMA_BASE_URL

    all_results = {}

    # ── Ollama ────────────────────────────────────────────────────────────────
    if not args.watsonx_only:
        env_models = os.getenv("BENCH_OLLAMA_MODELS", "").strip()
        if env_models:
            ollama_models = [m.strip() for m in env_models.split(",") if m.strip()]
        else:
            ollama_models = _get_ollama_models(base_url)

        if not ollama_models:
            print("[bench] No Ollama models found — is Ollama running?")
        else:
            print(f"\n{'='*80}")
            print(f" OLLAMA BENCHMARK  ({len(ollama_models)} models × {len(prompts)} prompts)")
            print(f"{'='*80}")
            for model in ollama_models:
                print(f"\n── {model} ──────────────────────────────────────────")
                stats = _bench_one_ollama(model, base_url, prompts)
                _print_summary("ollama", model, stats)
                all_results[f"ollama/{model}"] = stats

    # ── Watsonx ───────────────────────────────────────────────────────────────
    if not args.ollama_only and _llm.LLM_API_KEY:
        env_models = os.getenv("BENCH_WATSONX_MODELS",
                               "meta-llama/llama-3-3-70b-instruct,"
                               "meta-llama/llama-3-2-3b-instruct,"
                               "ibm/granite-3-3-8b-instruct").strip()
        wx_models = [m.strip() for m in env_models.split(",") if m.strip()]
        print(f"\n{'='*80}")
        print(f" WATSONX BENCHMARK  ({len(wx_models)} models × {len(prompts)} prompts)")
        print(f"{'='*80}")
        for model_id in wx_models:
            print(f"\n── {model_id} ──────────────────────────────────────────")
            stats = _bench_one_watsonx(model_id, prompts)
            _print_summary("watsonx", model_id, stats)
            all_results[f"watsonx/{model_id}"] = stats
    elif not args.ollama_only and not _llm.LLM_API_KEY:
        print("\n[bench] Skipping Watsonx — LLM_API_KEY not set")

    # ── Final comparison table ─────────────────────────────────────────────────
    if len(all_results) > 1:
        print(f"\n{'='*80}")
        print(" OVERALL COMPARISON")
        print(f"{'='*80}")
        print(f"  {'Backend':<10} {'Model':<36} {'p50(ms)':>8} {'p95(ms)':>8} "
              f"{'valid%':>7} {'exprs':>6} {'stop':>5} {'db2':>4}")
        print(f"  {'-'*10} {'-'*36} {'-'*8} {'-'*8} {'-'*7} {'-'*6} {'-'*5} {'-'*4}")
        for key, stats in sorted(all_results.items(),
                                  key=lambda kv: kv[1]["p50_ms"]):
            backend, model = key.split("/", 1)
            stop_s = "✓" if stats["stop_ok"] else "✗"
            db2_s  = "✓" if stats["db2_ok"]  else "✗"
            print(f"  {backend:<10} {model:<36} {stats['p50_ms']:>8.0f} {stats['p95_ms']:>8.0f} "
                  f"  {stats['valid_pct']:>5.0f}% {stats['expr_count']:>6} {stop_s:>5} {db2_s:>4}")
        best = min(all_results.items(), key=lambda kv: kv[1]["p50_ms"])
        print(f"\n  ★ Fastest (p50): {best[0]}  ({best[1]['p50_ms']:.0f} ms)")

    if args.save:
        Path(args.save).write_text(json.dumps(all_results, indent=2))
        print(f"\n[bench] results saved → {args.save}")


if __name__ == "__main__":
    main()
