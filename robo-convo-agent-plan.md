# Robo Conversation Agent — MCP Service Plan

## Top-Level Overview

Build a **new standalone MCP service** (`robo-convo-agent/`) that gives the Otto ESP32 robot
a real-time, VAD-driven voice conversation loop:

1. **Listen** — capture mic audio from Mac (or phone via mic-sharing) using **Voice Activity
   Detection (WebRTC VAD / Silero VAD)** so it records only while the user is speaking and
   stops automatically when they finish — no fixed-duration recording.
2. **Transcribe** — run speech-to-text on the captured audio with **faster-whisper 1.2.1**
   (already installed). Default model: `distil-small.en` — a distilled student of Whisper
   large, ~200–350 ms on CPU int8, best speed/quality balance for real-time conversation.
   `vad_filter=True` passed to `transcribe()` to strip silence padding before inference.
3. **Plan** — send transcript to a **fast local LLM via Ollama** (default `qwen2.5:3b` or
   `llama3.2:3b`) that returns a JSON object: `{reply_text, expression, move_cmd,
   show_text, should_stop}`.  Falls back to Watsonx if `OLLAMA_BASE_URL` is not set.
4. **Act** — in parallel: set robot emoji expression, show ticker text on screen, play TTS
   audio via the Mac TTS pipeline (`/mactts`), execute movement command.

This is **purely robot-interaction** — no meeting capture, no MoM, no Teams integration.
It is a self-contained replacement/evolution of `conversation-agent/` with VAD + Ollama.

### New Directory
```
BOB_ASSISTANT/
  robo-convo-agent/
    server.py          ← FastMCP server  (only file changed at runtime)
    vad.py             ← VAD mic capture helper
    llm.py             ← Ollama / Watsonx LLM wrapper
    tts.py             ← Mac TTS → WAV → POST /mactts pipeline
    robot.py           ← robot HTTP helpers (expressions, move, ticker, screen)
    Dockerfile
    requirements.txt
```

---

## Architecture Flow

```
User speaks
    │
    ▼
[VAD Mic Capture] (vad.py)
  sounddevice + webrtcvad
  records frames while voice active
  stops after N ms of silence
    │
    ▼
[Whisper STT] (faster-whisper, distil-small.en, int8, vad_filter=True)
    │
    ▼
[LLM Planner] (llm.py)
  Ollama qwen2.5:3b  ←→  Watsonx fallback
  returns JSON:
  { reply_text, expression, move_cmd, show_text, should_stop }
    │
    ├──► [robot.py] set_expression()   /eyes?m=N
    ├──► [robot.py] robot_ticker()     /ticker?text=…
    ├──► [tts.py]   robot_speak()      say → ffmpeg → POST /mactts
    └──► [robot.py] robot_move()       /move?cmd=…   (if non-null)
```

---

## Sub-Tasks

---

### Sub-Task 1 — VAD Mic Capture Module (`vad.py`)

**Intent**
Replace the fixed-duration `ffmpeg -t N` mic recording with a real-time VAD loop.
The user speaks freely; capture ends automatically after a configurable silence gap
(default 800 ms). This removes the artificial `LISTEN_DURATION` ceiling.

**Expected Outcomes**
- `vad.py` exports a single function `capture_until_silence(out_path, sample_rate,
  silence_ms, max_duration_s) → bool`
- Internally uses `sounddevice` (already in `.venv`) to read mic frames at 16 kHz
- Uses `webrtcvad` (Google's VAD) at aggressiveness level 3 (most aggressive — cuts
  background noise fastest) to classify each 20 ms frame as speech / non-speech
- Stops recording when `silence_ms` of consecutive non-speech frames have elapsed
  after at least one speech frame has been detected
- Hard cap at `max_duration_s` (default 30 s) to prevent runaway capture
- Writes 16-bit mono PCM WAV to `out_path` at **16000 Hz** (matches robot I2S hardware
  and Whisper's native sample rate — no resampling overhead anywhere in the pipeline)
- Returns `True` if speech was detected, `False` if only silence or mic error

**Todo List**
1. Create `robo-convo-agent/vad.py`
2. Import `sounddevice`, `webrtcvad`, `wave`, `numpy`
3. Implement `capture_until_silence()` with a ring-buffer of VAD results
4. Write pre-speech frames too (300 ms look-ahead buffer) to avoid clipping word starts
5. Handle `sounddevice.PortAudioError` gracefully (log + return False)

**Relevant Context**
- `sounddevice` is already installed in `.venv` (confirmed in site-packages)
- `faster_whisper` ships its own `vad.py` — used as a *post-capture* filter via
  `vad_filter=True` in `transcribe()`, not for real-time frame classification
- Existing fixed-capture: `conversation-agent/server.py` `_capture_mic()` lines 153-163
- Sample rate: 16 kHz — robot I2S is 16 kHz (confirmed in `.ino`), Whisper native is 16 kHz
- `webrtcvad` must be added to `requirements.txt`

**Status** `[ ] pending`

---

### Sub-Task 2 — LLM Wrapper (`llm.py`)

**Intent**
Abstract the LLM backend so the agent can use either a fast local Ollama model
(`qwen2.5:3b` or similar) or fall back to Watsonx. The planner prompt and JSON
contract stay identical to the existing `conversation-agent` so the robot action
layer is unchanged.

**Expected Outcomes**
- `llm.py` exports `plan_response(messages: list[dict]) -> dict`
- If `OLLAMA_BASE_URL` is set (e.g. `http://localhost:11434`), calls Ollama's
  `/api/chat` endpoint with `stream=false`
- Otherwise calls Watsonx (copied from existing `_get_iam_token` / POST pattern)
- Returns parsed `{reply_text, expression, move_cmd, show_text, should_stop}` dict
- Handles JSON parse failures with a safe fallback
- Ollama call must have `timeout=15` (fast model, should respond in < 2 s locally)
- System prompt instructs the model to return exactly the required JSON keys

**Todo List**
1. Create `robo-convo-agent/llm.py`
2. Implement `_ollama_plan()` using `httpx` or `requests` to POST to Ollama `/api/chat`
3. Implement `_watsonx_plan()` copied from existing `planner_node` in
   `conversation-agent/server.py` lines 188-259
4. Implement public `plan_response()` that picks Ollama or Watsonx based on env var
5. Add `show_text` key to the JSON contract (new field: short string to display on
   ticker, max 26 chars; LLM may abbreviate `reply_text` here)

**Relevant Context**
- Existing Watsonx call: `conversation-agent/server.py` lines 204-218
- Existing IAM token helper: `conversation-agent/server.py` lines 42-56
- Ollama OpenAI-compatible API: `POST /api/chat` with `{"model":…,"messages":…,"stream":false}`
- Model recommendation: `qwen2.5:3b` (very fast, good JSON adherence) or `llama3.2:3b`
- `OLLAMA_BASE_URL` defaults to `http://localhost:11434` if set at all

**Status** `[ ] pending`

---

### Sub-Task 3 — Robot HTTP Helpers (`robot.py`)

**Intent**
Extract all robot HTTP interactions into a clean, reusable module. This mirrors
`bob-robo-service/server.py` but is a plain helper (not an MCP service itself).

**Expected Outcomes**
- `robot.py` exports: `set_expression(expr)`, `robot_move(cmd)`, `robot_ticker(text)`,
  `show_screen(mode)`, `robot_get(path)`
- All functions are fire-and-forget: errors are logged, not raised
- Expression map identical to `bob-robo-service/server.py` (`happy:0 … normal:6`)
- `robot_move` timeout is 25 s for dance commands, 8 s otherwise

**Todo List**
1. Create `robo-convo-agent/robot.py`
2. Copy `EXPR` dict and `_robot_get`, `_set_expression`, `_robot_move`, `_show_screen`,
   `_robot_ticker` from `conversation-agent/server.py` and `bob-robo-service/server.py`
3. Expose them as public functions (drop underscore prefix)
4. Read `ROBOT_HOST` from env with same default `192.168.1.11`

**Relevant Context**
- `bob-robo-service/server.py` lines 21-138 — definitive source
- `conversation-agent/server.py` lines 78-135 — has same helpers inline
- Robot endpoints used: `/eyes?m=N`, `/move?cmd=X`, `/ticker?text=X`, `/screen/MODE`

**Status** `[ ] pending`

---

### Sub-Task 4 — TTS Pipeline (`tts.py`)

**Intent**
Wrap the Mac TTS → ffmpeg → `/mactts` pipeline in a standalone module so it can be
called from the conversation loop. Identical pipeline to `conversation-agent` but
extracted as a module. Add a `play_locally` flag to also play TTS on the Mac speaker
(for testing without the robot).

**Expected Outcomes**
- `tts.py` exports `robot_speak(text, play_locally=False) -> None`
- `say` → AIFF → ffmpeg boost → WAV at **16000 Hz** → POST `/mactts`
- Sample rate lowered from 22050 → **16000 Hz**: robot I2S DAC is 16 kHz so 22050 Hz
  was wasting ~38% upload bandwidth with no audible benefit
- If `play_locally=True`, also plays the WAV via `afplay` on macOS before sending
- Word-count-based timeout (same formula as existing code)
- TTS voice: `say` command (macOS built-in, ~100–200 ms generation — already fastest
  available; no new TTS packages needed)

**Todo List**
1. Create `robo-convo-agent/tts.py`
2. Copy `_robot_speak` from `conversation-agent/server.py` lines 85-119
3. Change `-ar` ffmpeg flag from `22050` → `16000`
4. Add `play_locally` parameter with `subprocess.run(["afplay", wav_path])` branch
5. Read `TTS_VOICE`, `ROBOT_HOST`, `FFMPEG` from env

**Relevant Context**
- `conversation-agent/server.py` `_robot_speak()` lines 85-119 — exact implementation
- `mac_tts_server.py` — reference for ffmpeg filter chain

**Status** `[ ] pending`

---

### Sub-Task 5 — Main Conversation Loop + MCP Server (`server.py`)

**Intent**
Wire VAD → Whisper → LLM → Act into a LangGraph state machine and expose it as an MCP
server. This is the new `robo-convo-agent` entry point. The graph is simpler than
`conversation-agent` because the `listen` node now uses VAD (not fixed-time) and the
`planner` node uses Ollama by default.

**Expected Outcomes**

MCP tools exposed:
| Tool | Parameters | Description |
|---|---|---|
| `start_conversation` | greeting, max_turns | Speak greeting, enter VAD loop |
| `stop_conversation` | thread_id | Gracefully end conversation |
| `get_conversation_state` | thread_id | Return LangGraph checkpoint |
| `speak_text` | text, play_locally | One-shot TTS without conversation |
| `set_robot_expression` | expression | Set face emoji directly |
| `move_robot` | move_cmd | Move/dance directly |
| `show_ticker` | text | Display text on robot screen |
| `robot_status` | — | Check robot reachability |
| `shutdown_robot` | — | Send stand + normal + idle, robot goes to rest |

LangGraph nodes:
- `listen_node` — shows **"Listening... speak now"** on ticker + sets screen to `listen`
  mode, calls `vad.capture_until_silence()`; when VAD stops immediately switches ticker
  to **"Thinking..."** + screen to `idle`; then transcribes with Whisper
- `planner_node` — calls `llm.plan_response()`; extracts reply/expr/move/show_text
- `act_node` — sets screen to `speak`, sets expression, puts `show_text` (or truncated
  reply) on ticker, speaks TTS, executes move; after TTS completes loops back to `listen`

**Screen Feedback Sequence (per turn):**
```
Entry to listen_node:
  ticker  → "Listening... speak now"
  screen  → listen   (ear/waveform animation on robot display)

VAD detects end of speech:
  ticker  → "Thinking..."
  screen  → idle     (neutral face while LLM processes)

act_node fires:
  ticker  → show_text (LLM-chosen short label, ≤26 chars)
  screen  → speak    (mouth animation)
  [TTS plays]

After TTS completes:
  screen  → listen   (ready for next turn)
  ticker  → "Listening... speak now"
```

**Todo List**
1. Create `robo-convo-agent/server.py`
2. Import `vad`, `llm`, `robot`, `tts` modules
3. Define `ConversationState` TypedDict (same as existing, add `show_text` field)
4. Implement `listen_node` — ticker "Listening... speak now" before VAD, ticker
   "Thinking..." immediately after VAD completes, then Whisper transcription
5. Implement `planner_node` and `act_node` using new modules
6. Wire LangGraph: listen → planner → act → (continue | end)
7. Define all 9 MCP tools listed above
8. Support `MCP_TRANSPORT=http` (default stdio) identical to existing services

**Relevant Context**
- `conversation-agent/server.py` — base implementation to evolve from
- `bob-robo-service/server.py` — direct robot tool patterns to copy for one-shot tools
- Port: `8006` (next free after `8005` used by existing conversation-agent in compose)

**Status** `[ ] pending`

---

### Sub-Task 6 — Packaging (`requirements.txt`, `Dockerfile`, `.env` additions)

**Intent**
Make the new service installable and runnable. Add only new dependencies (VAD-specific).
Update `.env.example` with Ollama vars. Add service entry to `docker-compose.yml`.

**Expected Outcomes**
- `requirements.txt` lists: `fastmcp`, `faster-whisper`, `sounddevice`, `webrtcvad`,
  `requests`, `python-dotenv`, `langgraph`, `httpx`, `numpy`
- `Dockerfile` based on `python:3.11-slim` with ffmpeg + PortAudio system deps
- `.env.example` gains `OLLAMA_BASE_URL`, `OLLAMA_MODEL`, `VAD_SILENCE_MS`,
  `VAD_MAX_DURATION_S` entries
- `docker-compose.yml` gains `robo-convo-agent` service on port `8006`

**Todo List**
1. Create `robo-convo-agent/requirements.txt`
2. Create `robo-convo-agent/Dockerfile`
3. Add Ollama + VAD vars to `BOB_ASSISTANT/.env.example`
4. Add `robo-convo-agent` block to `BOB_ASSISTANT/docker-compose.yml`

**Relevant Context**
- Existing `docker-compose.yml` service pattern (lines 54-66 for conversation-agent)
- `sounddevice` requires `libportaudio2` (Linux) or `portaudio` (Homebrew on Mac)
- `webrtcvad` requires no system deps (pure Python + C extension)
- Ollama container: `ollama/ollama:latest` — can be added to compose as an optional
  service with a `profiles: [ollama]` guard so it does not start by default

**Status** `[ ] pending`

---

## Key Decisions / Trade-offs

| Decision | Chosen Approach | Rationale |
|---|---|---|
| VAD library | `webrtcvad` (Google) | Battle-tested, 20 ms frame VAD, pure Python install, already used in many voice projects |
| Local LLM | Ollama `qwen2.5:3b` | 3 B param model runs entirely on CPU/GPU locally, sub-2 s response, good JSON adherence |
| LLM fallback | Watsonx (existing auth) | Zero new infrastructure for cloud path |
| TTS | Unchanged Mac TTS pipeline | Already proven to work with the robot audio hardware |
| Whisper model | `base.en` default | Fast on CPU (< 1 s for short utterances); user can set `WHISPER_MODEL=small` for quality |
| Service port | `8006` | Next free port in the compose stack |
| Meeting features | Not included | Explicitly excluded per requirements |

## Files Changed / Created

| File | Action |
|---|---|
| `BOB_ASSISTANT/robo-convo-agent/vad.py` | CREATE |
| `BOB_ASSISTANT/robo-convo-agent/llm.py` | CREATE |
| `BOB_ASSISTANT/robo-convo-agent/robot.py` | CREATE |
| `BOB_ASSISTANT/robo-convo-agent/tts.py` | CREATE |
| `BOB_ASSISTANT/robo-convo-agent/server.py` | CREATE |
| `BOB_ASSISTANT/robo-convo-agent/requirements.txt` | CREATE |
| `BOB_ASSISTANT/robo-convo-agent/Dockerfile` | CREATE |
| `BOB_ASSISTANT/.env.example` | MODIFY (add Ollama + VAD vars) |
| `BOB_ASSISTANT/docker-compose.yml` | MODIFY (add robo-convo-agent service) |
