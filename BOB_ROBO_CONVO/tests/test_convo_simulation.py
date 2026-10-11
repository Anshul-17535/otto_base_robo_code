#!/usr/bin/env python3
"""
test_convo_simulation.py — Full end-to-end conversation simulation without a robot.

Starts the static_robo.py mock server on a free port, overrides ROBOT_HOST so all
robo_robot.py calls go to the mock, then drives a scripted multi-turn conversation
through the full pipeline:

    text input  →  robo_llm.plan_response()
                →  robo_tts.robot_speak() (audio goes to mock /mactts → afplay)
                →  robo_robot.set_expression() + robot_ticker() + show_screen()
                →  optional: robo_robot.play_move()

No microphone or Whisper is involved — transcripts are injected directly to test
the LLM→action pipeline. Whisper is tested separately (see test_tts_local.py).

Usage:
    cd BOB_ROBO_CONVO

    # Full simulation (audio plays through Mac speaker):
    .venv/bin/python tests/test_convo_simulation.py

    # Silent mode (no audio playback):
    .venv/bin/python tests/test_convo_simulation.py --no-play

    # Only run unit tests (no LLM calls, no audio):
    .venv/bin/python tests/test_convo_simulation.py --test

    # Custom conversation script from file:
    .venv/bin/python tests/test_convo_simulation.py --script my_script.json

Environment:
    MOCK_ROBO_PORT   port for the mock robot server (default: 8092)
    OLLAMA_BASE_URL  Ollama endpoint (uses Watsonx if unset)
    TTS_PLAY_LOCALLY set to 1 to hear audio through Mac speakers
"""
from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest.mock import patch, MagicMock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # for static_robo

from dotenv import load_dotenv
load_dotenv(dotenv_path=Path(__file__).resolve().parents[1] / ".env", override=True)

import robo_llm   as _llm
import robo_robot as _robot
import robo_tts   as _tts

# ── Default conversation script ───────────────────────────────────────────────
DEFAULT_SCRIPT = [
    ("greeting",     "Hi Bob! How are you today?"),
    ("joke",         "Tell me a quick joke!"),
    ("move_request", "Can you do a happy wiggle for me?"),
    ("photo",        "Take a photo please!"),
    ("db2",          "How do I check the status of a Db2 tablespace?"),
    ("weather",      "What's the weather on Mars?"),
    ("goodbye",      "Okay, goodbye!"),
]

# ── Timing thresholds (ms) ─────────────────────────────────────────────────────
LLM_TIMEOUT_MS  = 25_000   # LLM must respond within 25 s
TTS_TIMEOUT_MS  = 15_000   # TTS render + upload must finish within 15 s per phrase
TURN_TIMEOUT_MS = 45_000   # full turn (LLM + TTS + robot actions) within 45 s


def _free_port() -> int:
    """Find a free TCP port."""
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# ── In-process lightweight mock (for unit tests that don't need the real mock) ─
class _InlineMockHandler(BaseHTTPRequestHandler):
    log_calls = []

    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        self.__class__.log_calls.append(("GET", self.path))
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        if "/movelist" in self.path:
            self.wfile.write(b'[{"n":8,"name":"Happy wiggle"},{"n":22,"name":"Leg wave"}]')
        elif "/state" in self.path:
            self.wfile.write(b'{"scaredMode":0,"react":0,"pitch":0,"roll":0}')
        elif "/hw" in self.path:
            self.wfile.write(b'{"ok":true,"mock":true}')
        elif "/record" in self.path:
            # Return 1 s of silence WAV
            import io, wave as wv
            buf = io.BytesIO()
            with wv.open(buf, "wb") as wf:
                wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(16000)
                wf.writeframes(b"\x00\x00" * 16000)
            data = buf.getvalue()
            self.wfile.write(data)
            return
        else:
            self.wfile.write(b'{"ok":true}')

    def do_POST(self):
        self.__class__.log_calls.append(("POST", self.path))
        length = int(self.headers.get("Content-Length", 0))
        if length:
            self.rfile.read(length)
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"ok":true}')


def _start_inline_mock() -> tuple[ThreadingHTTPServer, int]:
    """Start a minimal in-process mock robot server. Returns (server, port)."""
    port = _free_port()
    server = ThreadingHTTPServer(("127.0.0.1", port), _InlineMockHandler)
    t = threading.Thread(target=server.serve_forever, daemon=True)
    t.start()
    return server, port


# ── Conversation runner ───────────────────────────────────────────────────────
class ConvoSimulation:
    """
    Drives a scripted conversation through the real LLM + TTS + robot action
    pipeline, with all HTTP calls going to the mock robot server.
    """

    def __init__(self, mock_port: int, play_locally: bool = False,
                 verbose: bool = True):
        self.mock_port    = mock_port
        self.play_locally = play_locally
        self.verbose      = verbose
        self.history: list[dict] = []
        self.timings: list[dict] = []

    def _log(self, msg: str):
        if self.verbose:
            print(f"  [{time.strftime('%H:%M:%S')}] {msg}", flush=True)

    def _set_mock_host(self):
        """Point robo_robot at the mock server for this simulation run."""
        _robot.ROBOT_BASE_URL = f"http://127.0.0.1:{self.mock_port}"
        _robot.ROBOT_HOST     = f"127.0.0.1:{self.mock_port}"
        _tts.TTS_ENDPOINT     = "/mactts"

    def run_turn(self, tag: str, transcript: str) -> dict:
        """Execute one conversation turn: LLM → robot actions → TTS."""
        self._log(f"[{tag}] USER: {transcript!r}")
        self.history.append({"role": "user", "content": transcript})

        timing = {"tag": tag, "transcript": transcript}

        # ── Stage 1: LLM ─────────────────────────────────────────────────────
        t_llm = time.perf_counter()
        plan  = _llm.plan_response(self.history, transcript)
        timing["llm_ms"] = (time.perf_counter() - t_llm) * 1000

        self._log(f"  LLM {timing['llm_ms']:.0f}ms → "
                  f"reply={plan.get('reply_text','')[:50]!r}  "
                  f"expr={plan.get('expression')}  "
                  f"move={plan.get('move_cmd')}  "
                  f"stop={plan.get('should_stop')}  "
                  f"db2={plan.get('need_outside_help')}")

        # ── Stage 2: Robot actions (expression + ticker + screen) ─────────────
        t_robot = time.perf_counter()
        try:
            _robot.set_expression(plan.get("expression", "normal"))
            _robot.show_screen("speak")
            _robot.robot_ticker(plan.get("show_text", "")[:26])
        except Exception as exc:
            self._log(f"  robot action error: {exc}")
        timing["robot_ms"] = (time.perf_counter() - t_robot) * 1000

        # ── Stage 3: Optional move ────────────────────────────────────────────
        move_cmd = plan.get("move_cmd")
        timing["move"] = move_cmd
        if move_cmd:
            t_move = time.perf_counter()
            idx = next((m["n"] for m in _robot.list_moves()
                        if m.get("name") == move_cmd), None)
            if idx is not None:
                _robot.play_move(idx)
            else:
                _robot.robot_move(move_cmd)
            timing["move_ms"] = (time.perf_counter() - t_move) * 1000

        # ── Stage 4: TTS ──────────────────────────────────────────────────────
        reply_text = plan.get("reply_text", "")
        if plan.get("need_outside_help"):
            # Simulate outside-help (use the LLM's bridge phrase, skip real call)
            self._log("  [db2 routing] outside help simulated (mock)")
            reply_text = plan.get("reply_text", "Let me ask my Db2 expert!")

        t_tts = time.perf_counter()
        try:
            ok = _tts.robot_speak(reply_text, play_locally=self.play_locally)
            timing["tts_ok"] = ok
        except Exception as exc:
            self._log(f"  TTS error: {exc}")
            timing["tts_ok"] = False
        timing["tts_ms"] = (time.perf_counter() - t_tts) * 1000
        timing["total_ms"] = timing["llm_ms"] + timing.get("move_ms", 0) + timing["tts_ms"]

        self._log(f"  TTS {timing['tts_ms']:.0f}ms  total {timing['total_ms']:.0f}ms")

        # ── Add bot reply to history ──────────────────────────────────────────
        self.history.append({"role": "assistant", "content": reply_text})
        timing["reply"] = reply_text
        timing["plan"]  = plan
        self.timings.append(timing)

        return timing

    def run(self, script: list[tuple[str, str]]) -> list[dict]:
        """Run the full scripted conversation. Returns list of turn timings."""
        self._set_mock_host()
        # Load available moves into the LLM's prompt
        moves = [m.get("name") for m in _robot.list_moves() if m.get("name")]
        _llm.set_available_moves(moves)

        print(f"\n{'='*70}")
        print(f" CONVERSATION SIMULATION  ({len(script)} turns)")
        print(f" Mock robot: http://127.0.0.1:{self.mock_port}")
        print(f" LLM: {'Ollama '+_llm.OLLAMA_MODEL if _llm.USE_OLLAMA else 'Watsonx '+_llm.LLM_MODEL}")
        print(f" Audio: {'Mac speaker (afplay)' if self.play_locally else 'silent (mock /mactts)'}")
        print(f"{'='*70}\n")

        for tag, transcript in script:
            turn = self.run_turn(tag, transcript)
            if turn["plan"].get("should_stop"):
                self._log("  [stop] Conversation ended by user")
                break

        return self.timings

    def print_summary(self):
        if not self.timings:
            return
        print(f"\n{'='*70}")
        print(" TURN-BY-TURN LATENCY SUMMARY")
        print(f"{'='*70}")
        print(f"  {'Tag':<14} {'LLM(ms)':>8} {'TTS(ms)':>8} {'Total(ms)':>10} {'Reply'}")
        print(f"  {'-'*14} {'-'*8} {'-'*8} {'-'*10} {'-'*40}")
        for t in self.timings:
            reply_short = t.get("reply","")[:40]
            print(f"  {t['tag']:<14} {t['llm_ms']:>8.0f} {t['tts_ms']:>8.0f} "
                  f"{t['total_ms']:>10.0f}  {reply_short!r}")
        llm_times  = [t["llm_ms"]  for t in self.timings]
        tts_times  = [t["tts_ms"]  for t in self.timings]
        total_times = [t["total_ms"] for t in self.timings]
        print(f"\n  avg LLM:   {sum(llm_times)/len(llm_times):.0f}ms")
        print(f"  avg TTS:   {sum(tts_times)/len(tts_times):.0f}ms")
        print(f"  avg total: {sum(total_times)/len(total_times):.0f}ms")
        print(f"  p95 total: {sorted(total_times)[int(len(total_times)*0.95)]:.0f}ms")


# ── Unit tests (no LLM calls; uses mocked responses) ─────────────────────────
class ConvoSimTests(unittest.TestCase):

    def setUp(self):
        self._server, self._port = _start_inline_mock()
        _InlineMockHandler.log_calls.clear()
        _robot.ROBOT_BASE_URL = f"http://127.0.0.1:{self._port}"
        _robot.ROBOT_HOST     = f"127.0.0.1:{self._port}"
        _tts.TTS_ENDPOINT     = "/mactts"

    def tearDown(self):
        self._server.shutdown()

    def test_mock_robot_health(self):
        """Mock robot returns ok=true on /health."""
        import requests
        r = requests.get(f"http://127.0.0.1:{self._port}/health", timeout=3)
        self.assertEqual(r.status_code, 200)
        self.assertTrue(r.json().get("ok"))

    def test_mock_movelist(self):
        """Mock robot returns movelist JSON."""
        import requests
        r = requests.get(f"http://127.0.0.1:{self._port}/movelist", timeout=3)
        self.assertEqual(r.status_code, 200)
        data = r.json()
        self.assertIsInstance(data, list)
        self.assertGreater(len(data), 0)
        self.assertIn("name", data[0])

    def test_mock_expression(self):
        """/eyes?m=N accepted by mock."""
        _robot.set_expression("happy")
        calls = [p for m, p in _InlineMockHandler.log_calls if "eyes" in p]
        self.assertTrue(any("eyes?m=0" in p for p in calls), f"calls: {calls}")

    def test_mock_ticker(self):
        """robot_ticker() hits mock /ticker endpoint."""
        _robot.robot_ticker("hello world")
        calls = [p for m, p in _InlineMockHandler.log_calls if "ticker" in p]
        self.assertTrue(any("ticker" in p for p in calls))

    def test_mock_screen(self):
        """show_screen() hits the mock /screen endpoint."""
        _robot.show_screen("speak")
        calls = [p for m, p in _InlineMockHandler.log_calls]
        self.assertTrue(any("/screen/speak" in p for p in calls))

    def test_tts_sends_to_mock(self):
        """robot_speak() POSTs audio to mock /mactts."""
        with patch.object(_tts, "_render_to_aiff_or_wav",
                          side_effect=lambda t, d: d / "s.aiff"), \
             patch.object(_tts, "_to_pcm",
                          return_value=b"\x00\x02" * 1600):
            ok = _tts.robot_speak("Hello!", play_locally=False)
        self.assertTrue(ok)
        calls = [p for m, p in _InlineMockHandler.log_calls if m == "POST"]
        self.assertTrue(any("/mactts" in p for p in calls), f"POST calls: {calls}")

    def test_llm_parse_goodbye(self):
        """LLM parser returns should_stop=True for goodbye transcripts."""
        result = _llm._parse('{"reply_text":"Bye!","expression":"happy",'
                             '"move_cmd":null,"show_text":"Bye!","should_stop":true,'
                             '"take_photo":false,"need_outside_help":false}', "goodbye")
        self.assertTrue(result["should_stop"])

    def test_llm_parse_db2(self):
        """LLM parser returns need_outside_help=True for Db2 transcripts."""
        result = _llm._parse('{}', "how do I run RUNSTATS on Db2?")
        self.assertTrue(result["need_outside_help"])

    def test_llm_parse_valid_expression(self):
        """LLM parser validates expression field."""
        result = _llm._parse('{"reply_text":"Hi","expression":"INVALID",'
                             '"move_cmd":null,"show_text":"","should_stop":false,'
                             '"take_photo":false,"need_outside_help":false}', "")
        self.assertEqual(result["expression"], "normal")

    def test_llm_parse_move_cmd_validation(self):
        """LLM parser case-insensitively matches move names."""
        _llm.set_available_moves(["Happy wiggle", "Leg wave"])
        result = _llm._parse('{"reply_text":"Dance!","expression":"happy",'
                             '"move_cmd":"happy wiggle","show_text":"","should_stop":false,'
                             '"take_photo":false,"need_outside_help":false}', "dance")
        self.assertEqual(result["move_cmd"], "Happy wiggle")

    def test_mock_robot_playmove(self):
        """play_move() is accepted by the mock."""
        ok = _robot.play_move(8)
        self.assertTrue(ok)
        calls = [p for m, p in _InlineMockHandler.log_calls]
        self.assertTrue(any("playmove" in p for p in calls))

    def test_convo_sim_one_turn_mock_llm(self):
        """Single turn with mocked LLM returns valid timing dict."""
        mock_plan = {
            "reply_text": "Hello! I am Anvyxo.",
            "expression": "happy",
            "move_cmd":   None,
            "show_text":  "Hello!",
            "should_stop": False,
            "take_photo":  False,
            "need_outside_help": False,
        }
        sim = ConvoSimulation(self._port, play_locally=False, verbose=False)
        sim._set_mock_host()
        with patch.object(_llm, "plan_response", return_value=mock_plan), \
             patch.object(_tts, "_render_to_aiff_or_wav",
                          side_effect=lambda t, d: d / "s.aiff"), \
             patch.object(_tts, "_to_pcm",
                          return_value=b"\x00\x02" * 1600):
            timing = sim.run_turn("test_hello", "Hi there!")
        self.assertIn("llm_ms", timing)
        self.assertIn("tts_ms", timing)
        self.assertFalse(timing["plan"]["should_stop"])
        self.assertEqual(timing["plan"]["reply_text"], "Hello! I am Anvyxo.")

    def test_full_script_mock_llm(self):
        """All 7 scripted turns complete without error (mocked LLM + TTS)."""
        responses = iter([
            {"reply_text": "Hi there!", "expression": "happy",
             "move_cmd": None, "show_text": "Hi!", "should_stop": False,
             "take_photo": False, "need_outside_help": False},
            {"reply_text": "Why did the robot cross the road?",
             "expression": "happy", "move_cmd": None, "show_text": "Joke time!",
             "should_stop": False, "take_photo": False, "need_outside_help": False},
            {"reply_text": "Sure, wiggle time!", "expression": "happy",
             "move_cmd": "Happy wiggle", "show_text": "Wiggling!",
             "should_stop": False, "take_photo": False, "need_outside_help": False},
            {"reply_text": "Say cheese!", "expression": "happy",
             "move_cmd": None, "show_text": "Say cheese!",
             "should_stop": False, "take_photo": True, "need_outside_help": False},
            {"reply_text": "Great Db2 question!", "expression": "surprised",
             "move_cmd": None, "show_text": "Asking Db2 expert...",
             "should_stop": False, "take_photo": False, "need_outside_help": True},
            {"reply_text": "Mars is very cold!", "expression": "normal",
             "move_cmd": None, "show_text": "About Mars", "should_stop": False,
             "take_photo": False, "need_outside_help": False},
            {"reply_text": "Goodbye!", "expression": "love",
             "move_cmd": None, "show_text": "Bye!", "should_stop": True,
             "take_photo": False, "need_outside_help": False},
        ])
        sim = ConvoSimulation(self._port, play_locally=False, verbose=False)
        with patch.object(_llm, "plan_response", side_effect=lambda *a, **kw: next(responses)), \
             patch.object(_tts, "_render_to_aiff_or_wav",
                          side_effect=lambda t, d: d / "s.aiff"), \
             patch.object(_tts, "_to_pcm",
                          return_value=b"\x00\x02" * 1600):
            timings = sim.run(DEFAULT_SCRIPT)
        # Should stop after "goodbye" (last turn)
        self.assertEqual(len(timings), 7)
        self.assertTrue(timings[-1]["plan"]["should_stop"])


# ── Interactive simulation CLI ────────────────────────────────────────────────
def _start_static_robo(port: int) -> subprocess.Popen | None:
    """Start the full static_robo.py mock as a subprocess."""
    static_robo = Path(__file__).resolve().parents[1] / "static_robo.py"
    if not static_robo.exists():
        print(f"[sim] static_robo.py not found at {static_robo} — skipping subprocess mock")
        return None
    venv_py = Path(__file__).resolve().parents[1] / ".venv" / "bin" / "python"
    py = str(venv_py) if venv_py.exists() else sys.executable
    return subprocess.Popen(
        [py, str(static_robo), "--port", str(port)],
        stderr=subprocess.DEVNULL,
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-play", action="store_true",
                    help="No audio output (silent mode — mock robot receives but ignores audio)")
    ap.add_argument("--test", action="store_true",
                    help="Run unittest suite only (no LLM/TTS calls)")
    ap.add_argument("--script", metavar="FILE",
                    help="JSON file with [[tag, transcript], ...] conversation script")
    ap.add_argument("--mock-port", type=int,
                    default=int(os.getenv("MOCK_ROBO_PORT", "8092")))
    args = ap.parse_args()

    if args.test:
        unittest.main(argv=[sys.argv[0]], verbosity=2, exit=True)
        return

    script = DEFAULT_SCRIPT
    if args.script:
        script = [tuple(item) for item in json.loads(Path(args.script).read_text())]

    # Start the static_robo mock server
    port = args.mock_port
    proc = _start_static_robo(port)
    if proc is None:
        # Fall back to inline mock
        server, port = _start_inline_mock()
        print(f"[sim] Using inline mock on port {port}")
    else:
        time.sleep(0.8)   # wait for static_robo to start

    play = not args.no_play and os.getenv("TTS_PLAY_LOCALLY", "0") == "1"

    # Point robo_robot at the mock
    _robot.ROBOT_BASE_URL = f"http://127.0.0.1:{port}"
    _robot.ROBOT_HOST     = f"127.0.0.1:{port}"

    sim = ConvoSimulation(port, play_locally=play, verbose=True)
    try:
        timings = sim.run(script)
        sim.print_summary()
    finally:
        if proc:
            proc.terminate()


if __name__ == "__main__":
    main()
