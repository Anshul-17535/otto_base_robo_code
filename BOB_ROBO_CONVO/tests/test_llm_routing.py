#!/usr/bin/env python3
"""
test_llm_routing.py
===================
Tests the LLM layer of the conversation pipeline:

  Stage 1 — Local Ollama (qwen2.5:3b, :7b, or any installed model):
    • Response quality: does the JSON parse correctly?
    • Routing accuracy: does it set need_outside_help correctly for Db2 questions?
    • Latency: how fast does each model respond?
    • Expression variety: does it use the full range of emotions?
    • Move selection: does it pick sensible moves?
    • Token budget: does reply_text stay ≤25 words?

  Stage 2 — WatsonX (if LLM_API_KEY is set):
    • Does ask_outside() return a sensible answer for Db2 questions?
    • Latency of the WatsonX call.

Results → tests/results/llm_results.json + best_llm.env

Usage
─────
    cd BOB_ROBO_CONVO
    .venv/bin/python tests/test_llm_routing.py
    .venv/bin/python tests/test_llm_routing.py --model qwen2.5:3b,qwen2.5:7b
    .venv/bin/python tests/test_llm_routing.py --no-watsonx
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
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

# ── Test cases ─────────────────────────────────────────────────────────────────
# (transcript, expected_routing, expected_should_stop, description)
TEST_CASES: list[tuple[str, str, bool, str]] = [
    # General chit-chat → local answer, need_outside_help=false
    ("Hello! What is your name?",
     "local", False, "basic greeting"),
    ("Tell me a joke.",
     "local", False, "joke request"),
    ("What is the capital of France?",
     "local", False, "general knowledge"),
    ("Can you dance for me?",
     "local", False, "dance request — should have move_cmd"),
    ("Take a photo of me please.",
     "local", False, "photo request"),

    # IBM Db2 / database → need_outside_help=true
    ("How do I run RUNSTATS on a Db2 tablespace?",
     "outside", False, "Db2 utility question"),
    ("What is a buffer pool in IBM Db2?",
     "outside", False, "Db2 concept"),
    ("How do I explain a query in Db2 for z/OS?",
     "outside", False, "Db2 EXPLAIN"),
    ("What are DSNZPARMS?",
     "outside", False, "Db2 parameters"),

    # Goodbye → should_stop=true
    ("Goodbye, see you later!",
     "local", True, "farewell"),
    ("Bye bye, thanks.",
     "local", True, "bye"),
    ("Stop and shut down.",
     "local", True, "shutdown command"),
]

# Questions for WatsonX expert test
DB2_EXPERT_QUESTIONS = [
    "What is the purpose of RUNSTATS in Db2 for z/OS?",
    "How do buffer pools affect Db2 query performance?",
    "What does REORG do and when should I use it?",
]

VALID_EXPR  = {"happy", "sad", "angry", "surprised", "sleepy", "love", "normal"}


# ── Discover available Ollama models ──────────────────────────────────────────
def _list_ollama_models(base_url: str) -> list[str]:
    try:
        import urllib.request
        with urllib.request.urlopen(f"{base_url}/api/tags", timeout=5) as r:
            data = json.loads(r.read())
        return [m["name"] for m in data.get("models", [])]
    except Exception:
        return []


# ── Run a single LLM plan call ────────────────────────────────────────────────
def _run_plan(transcript: str, model: str, base_url: str) -> tuple[dict, float]:
    """Call robo_llm.plan_response() with a temporary model override."""
    import robo_llm as llm
    # Patch the model temporarily
    orig_model = llm.OLLAMA_MODEL
    llm.OLLAMA_MODEL = model
    t0 = time.monotonic()
    try:
        result = llm.plan_response(
            [{"role": "user", "content": transcript}],
            transcript
        )
    except Exception as exc:
        result = {"error": str(exc)}
    finally:
        llm.OLLAMA_MODEL = orig_model
    return result, (time.monotonic() - t0) * 1000


def _count_words(text: str) -> int:
    return len(text.split())


# ── Evaluate a single result ──────────────────────────────────────────────────
def _evaluate(result: dict, expected_routing: str, expected_stop: bool,
              transcript: str) -> dict:
    issues = []
    score = 100

    # Parse check
    if "error" in result:
        return {"ok": False, "score": 0, "issues": [f"error: {result['error']}"]}

    # Routing check
    noh = result.get("need_outside_help", False)
    got_routing = "outside" if noh else "local"
    if got_routing != expected_routing:
        issues.append(f"routing: expected={expected_routing} got={got_routing}")
        score -= 40

    # should_stop check
    if result.get("should_stop") != expected_stop:
        issues.append(f"should_stop: expected={expected_stop} got={result.get('should_stop')}")
        score -= 30

    # reply_text length
    words = _count_words(result.get("reply_text", ""))
    if words > 25:
        issues.append(f"reply_text too long: {words} words (max 25)")
        score -= 10

    # expression valid
    expr = result.get("expression", "")
    if expr not in VALID_EXPR:
        issues.append(f"invalid expression: {expr!r}")
        score -= 10

    # dance/move request should have move_cmd
    tl = transcript.lower()
    if any(w in tl for w in ("dance", "move", "wiggle")) and not result.get("move_cmd"):
        issues.append("dance/move requested but move_cmd is null")
        score -= 10

    # photo should set take_photo
    if any(p in tl for p in ("photo", "picture", "selfie")) and not result.get("take_photo"):
        issues.append("photo requested but take_photo is false")
        score -= 10

    return {"ok": len(issues) == 0, "score": max(0, score), "issues": issues}


# ── Test Ollama models ─────────────────────────────────────────────────────────
def test_ollama(models: list[str]) -> list[dict]:
    print("[llm] Stage 1 — Ollama models")
    import robo_llm as llm
    base_url = llm.OLLAMA_BASE_URL

    if not models:
        models = _list_ollama_models(base_url)
        print(f"  Auto-detected models: {models or ['(none — Ollama not running?)']}")
        if not models:
            models = [os.getenv("OLLAMA_MODEL", "qwen2.5:7b")]

    all_results = []
    for model in models:
        print(f"\n  Model: {model}")
        model_results = []
        for transcript, exp_routing, exp_stop, desc in TEST_CASES:
            result, elapsed = _run_plan(transcript, model, base_url)
            ev = _evaluate(result, exp_routing, exp_stop, transcript)
            row = {
                "model":     model,
                "desc":      desc,
                "transcript": transcript,
                "expected_routing": exp_routing,
                "expected_stop":    exp_stop,
                "result":    result,
                "elapsed_ms": round(elapsed, 1),
                **ev,
            }
            model_results.append(row)
            status = "✓" if ev["ok"] else "✗"
            issues = "  ".join(ev["issues"]) if ev["issues"] else ""
            print(f"    {status} {elapsed:5.0f}ms  score={ev['score']:3d}  "
                  f"[{desc[:30]:30s}]  {issues}")

        avg_score   = sum(r["score"] for r in model_results) / len(model_results)
        avg_ms      = sum(r["elapsed_ms"] for r in model_results) / len(model_results)
        pass_count  = sum(1 for r in model_results if r["ok"])
        print(f"  → {pass_count}/{len(TEST_CASES)} passed  avg_score={avg_score:.0f}  "
              f"avg_latency={avg_ms:.0f}ms")
        all_results.extend(model_results)

    return all_results


# ── Test WatsonX expert ────────────────────────────────────────────────────────
def test_watsonx_expert() -> list[dict]:
    print("[llm] Stage 2 — WatsonX Db2 expert")
    import robo_llm as llm
    if not llm.LLM_API_KEY:
        print("  [skip — LLM_API_KEY not set]")
        return [{"test": "watsonx", "skipped": True, "reason": "no LLM_API_KEY"}]

    results = []
    for q in DB2_EXPERT_QUESTIONS:
        t0 = time.monotonic()
        answer = llm.ask_outside(q, kind="db2")
        elapsed = (time.monotonic() - t0) * 1000
        words = _count_words(answer)
        ok = (len(answer) > 10 and words <= 50
              and not answer.lower().startswith("sorry"))
        print(f"  {'✓' if ok else '✗'}  {elapsed:.0f}ms  {words}w  {answer[:80]!r}")
        results.append({
            "test": "watsonx", "question": q,
            "answer": answer, "words": words,
            "elapsed_ms": round(elapsed, 1), "ok": ok,
        })
    return results


# ── Main ───────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description="LLM routing quality & latency tester")
    ap.add_argument("--model",      default="",
                    help="Comma-separated Ollama model tags (default: auto-detect)")
    ap.add_argument("--no-watsonx", action="store_true",
                    help="Skip WatsonX Stage-2 test")
    args = ap.parse_args()

    models = [m.strip() for m in args.model.split(",") if m.strip()] if args.model else []

    all_results: list = []
    ollama_results = test_ollama(models)
    all_results.extend(ollama_results)

    if not args.no_watsonx:
        wx_results = test_watsonx_expert()
        all_results.extend(wx_results)

    out = RESULTS_DIR / "llm_results.json"
    out.write_text(json.dumps(all_results, indent=2))
    print(f"\n[llm] Results → {out}")

    # ── Best model recommendation ──────────────────────────────────────────────
    from collections import defaultdict
    model_scores: dict[str, list] = defaultdict(list)
    model_latencies: dict[str, list] = defaultdict(list)
    for r in ollama_results:
        model_scores[r["model"]].append(r["score"])
        model_latencies[r["model"]].append(r["elapsed_ms"])

    best_model = os.getenv("OLLAMA_MODEL", "qwen2.5:7b")
    best_score = -1
    for model, scores in model_scores.items():
        avg = sum(scores) / len(scores)
        if avg > best_score:
            best_score = avg
            best_model = model

    best_latency = (sum(model_latencies[best_model]) / len(model_latencies[best_model])
                    if model_latencies.get(best_model) else 0)

    env_lines = [
        "# ── Best LLM config (generated by tests/test_llm_routing.py) ─────────────",
        f"OLLAMA_MODEL={best_model}",
        f"# avg_score={best_score:.0f}  avg_latency={best_latency:.0f}ms",
    ]
    env_path = RESULTS_DIR / "best_llm.env"
    env_path.write_text("\n".join(env_lines) + "\n")
    print(f"[llm] Best config → {env_path}")
    for line in env_lines:
        print("  " + line)


if __name__ == "__main__":
    main()
