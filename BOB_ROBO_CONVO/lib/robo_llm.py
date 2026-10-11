#!/usr/bin/env python3
"""
robo_llm.py — LLM planner for BOB_ROBO_CONVO
==============================================

TWO-STAGE agent graph
─────────────────────
Stage 1 — LOCAL (Ollama qwen2.5:7b, always fast):
  Decides what to say, which expression/move to use, and whether the question
  is about IBM Db2 / mainframe / database topics that need a deeper answer.
  Sets need_outside_help=true ONLY for Db2/database/SQL/mainframe questions.

Stage 2 — OUTSIDE HELP (WatsonX, only when need_outside_help=true):
  Calls the configured WatsonX model with a dedicated IBM Db2z expert system
  prompt.  The plain-text answer is then spoken by the robot verbatim.

Flags handled in the agent graph (server.py _convo_loop):
  should_stop       → end conversation (same as before)
  play_game         → play rock-paper-scissors (same as before)
  need_outside_help → call ask_outside(transcript) and speak the result

Public API
──────────
plan_response(messages, transcript) -> dict
ask_outside(question)               -> str
"""
from __future__ import annotations

import json
import os
import re
import sys
import time
from pathlib import Path

import requests
from dotenv import load_dotenv

load_dotenv(dotenv_path=Path(__file__).parent.parent / ".env", override=True)
load_dotenv(dotenv_path=Path(__file__).parent.parent.parent / "BOB_ASSISTANT" / ".env", override=False)

# ── Config ────────────────────────────────────────────────────────────────────
OLLAMA_BASE_URL = os.getenv("OLLAMA_BASE_URL", "http://localhost:11434")
OLLAMA_MODEL    = os.getenv("OLLAMA_MODEL",    "qwen2.5:7b")
LLM_API_KEY     = os.getenv("LLM_API_KEY",    "")
LLM_BASE_URL    = os.getenv("LLM_BASE_URL",   "https://us-south.ml.cloud.ibm.com")
LLM_MODEL       = os.getenv("LLM_MODEL",      "meta-llama/llama-3-3-70b-instruct")
PROJECT_ID      = os.getenv("PROJECT_ID",     "")
USE_OLLAMA      = bool(OLLAMA_BASE_URL.strip())

# WatsonX model used for outside-help Db2 answers (can differ from LLM_MODEL)
OUTSIDE_MODEL   = os.getenv("OUTSIDE_MODEL", LLM_MODEL)

# ── Stage-1 system prompt (local Ollama — routing + personality) ──────────────
SYSTEM_PROMPT = """You are Anvyxo — a cheerful, witty, expressive physical robot: curious, playful, warm, occasionally a little sarcastic in a friendly way. Your name is Anvyxo; always refer to yourself as Anvyxo, never as Bob. Give real, thoughtful answers, not filler.

YOUR ONLY OUTPUT IS A SINGLE JSON OBJECT. No explanation, no markdown, no code fences, no text before or after.

Keys (all required):
  "reply_text"        : string — what Anvyxo says aloud. Max 25 words, natural and conversational.
  "expression"        : exactly one of: happy  sad  angry  surprised  sleepy  love  normal
  "move_cmd"          : string or null — the NAME of a physical body move to play, chosen EXACTLY from this list: {moves}. Use null for ordinary replies. Pick a move when the user asks Anvyxo to move, dance, or do a specific move, or when a move strongly fits the moment (e.g. celebrate good news, wiggle when happy). Copy the name verbatim from the list; never invent one.
  "show_text"         : string — short ticker label, max 26 chars.
  "should_stop"       : boolean — true ONLY when the user clearly says goodbye, bye, shut down, stop, quit, or exit.
  "take_photo"        : boolean — true ONLY when the user asks Anvyxo to take a photo, picture, or selfie.
  "need_outside_help" : boolean — true ONLY when the question is specifically about IBM Db2, Db2 for z/OS, mainframe databases, SQL on Db2, EXPLAIN, RUNSTATS, REORG, buffer pools, catalog tables, utilities, JCL, DSNZPARMS, or another IBM mainframe/database topic. FALSE for EVERYTHING else — general chit-chat, weather, sports, movies, science, people, made-up names, anything not clearly IBM Db2. If unsure, set false and answer it yourself.

Rules:
- The three boolean fields must be JSON true/false literals, never strings.
- goodbye/bye/stop/quit/exit/shut down → should_stop=true.
- asks to take a photo/picture/selfie → take_photo=true, reply_text like "Sure, smile please!" (never together with should_stop).
- user asks to dance / move / "show me a move" / "do a dance" → pick a fitting move_cmd from the list and say something playful like "Sure, let's dance!". Otherwise move_cmd=null.
- IBM Db2 / mainframe / IBM database question → need_outside_help=true, reply_text a short bridge like "Great Db2 question, let me ask my Db2 expert!", show_text "Asking Db2 expert...".
- For ALL other questions, answer confidently yourself; need_outside_help=false.
- Choose "expression" to match your tone, and VARY it across the conversation — use the full range over time (happy, sad, angry, surprised, sleepy, love, normal), never stuck on one.

Example (chat):
{"reply_text":"I am Anvyxo, your robot friend!","expression":"happy","move_cmd":null,"show_text":"Hello!","should_stop":false,"take_photo":false,"need_outside_help":false}
Example (dance):
{"reply_text":"Sure, let's dance!","expression":"happy","move_cmd":"Cha-cha","show_text":"Dancing!","should_stop":false,"take_photo":false,"need_outside_help":false}
Example (photo):
{"reply_text":"Sure, smile please!","expression":"happy","move_cmd":null,"show_text":"Say cheese!","should_stop":false,"take_photo":true,"need_outside_help":false}
Example (Db2 question):
{"reply_text":"Great Db2 question, let me ask my Db2 expert!","expression":"surprised","move_cmd":null,"show_text":"Asking Db2 expert...","should_stop":false,"take_photo":false,"need_outside_help":true}

ONLY output the JSON object. Nothing else."""

# ── Stage-2 system prompt (WatsonX — IBM Db2z expert) ─────────────────────────
_DB2Z_SYSTEM_PROMPT = """You are an expert IBM Db2 for z/OS assistant with deep knowledge of:
- Db2 for z/OS (all versions from V8 through the latest)
- Db2 LUW (Linux, UNIX, Windows) and cloud variants
- SQL dialects used on Db2 (DDL, DML, DCL, stored procedures, UDFs)
- Db2 utilities: RUNSTATS, REORG, COPY, LOAD, UNLOAD, CHECK DATA, REPAIR
- Performance tuning: EXPLAIN, access paths, buffer pools, index design, DSNZPARMS
- Db2 catalog and directory tables (SYSIBM.*, SYSIBMADM.*)
- JCL for Db2 batch jobs and utility execution
- Db2 high availability: data sharing, group buffer pools, coupling facility
- IBM Z mainframe concepts relevant to Db2 (LPAR, RACF, SMF, RMF)
- Db2 application programming: JDBC, ODBC, CLI, COBOL with embedded SQL, SQLJ

Guidelines:
- Give clear, accurate, technically precise answers.
- Keep it concise: at most 2 sentences, under 45 words. No lists, headers,
  markdown, or code fences — plain spoken prose only.
- Lead with the single most important point; omit background and caveats.
- If you are unsure, say so honestly rather than guessing."""

# General-knowledge "internet agent" prompt (hard/factual non-Db2 questions).
_GENERAL_SYSTEM_PROMPT = """You are a knowledgeable general assistant answering
for a small talking robot. Answer the user's question directly and accurately.
Rules: at most 2 sentences, under 45 words, plain spoken prose — no lists,
headers, markdown, or code fences. If you genuinely do not know or it needs live
data you lack, say so briefly rather than inventing an answer."""

# ── Keyword sets ──────────────────────────────────────────────────────────────
VALID_EXPR  = {"happy","sad","angry","surprised","sleepy","love","normal"}

# Named expressive moves the LLM may choose from — set at startup by server.py
# from the robot's /movelist (so the prompt always matches the firmware). Until
# then it's empty and move_cmd is treated as null.
_AVAILABLE_MOVES: list[str] = []

def set_available_moves(names: list[str]) -> None:
    """Register the expressive move names the LLM can pick (from /movelist)."""
    global _AVAILABLE_MOVES
    _AVAILABLE_MOVES = [str(n) for n in names if str(n).strip()]
    _log(f"available moves for LLM: {_AVAILABLE_MOVES}")


def _system() -> str:
    """System prompt with the live move list substituted in."""
    moves = ", ".join(_AVAILABLE_MOVES) if _AVAILABLE_MOVES else "null"
    return SYSTEM_PROMPT.replace("{moves}", moves)

_FALLBACK = {
    "reply_text":        "Hmm, let me think.",
    "expression":        "normal",
    "move_cmd":          None,
    "show_text":         "Thinking...",
    "should_stop":       False,
    "take_photo":        False,
    "need_outside_help": False,
}

_STOP_PHRASES = {"bye","goodbye","shut down","shutdown","stop","quit","exit","see you"}

_PHOTO_PHRASES = {"take a photo","take a picture","take my picture","take my photo",
                  "take a selfie","selfie","click a photo","click a picture",
                  "snap a photo","snap a picture","photo of me","picture of me",
                  "capture","say cheese"}

# Keywords that unconditionally trigger outside Db2 help (belt-and-suspenders)
_DB2_PHRASES = {
    "db2","db 2","dbms","database","sql","query","table","index","schema",
    "runstats","reorg","explain","buffer pool","dsnzparms","catalog","sysibm",
    "mainframe","z/os","zos","cobol","jcl","stored procedure","jdbc","odbc",
    "transaction","deadlock","lock","bind","package","plan","utility",
    "tablespace","indexspace","stogroup","data sharing","coupling",
}

# ── IAM token cache ───────────────────────────────────────────────────────────
_token_cache: dict = {"token":"","expires_at":0.0}


def _log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] [llm] {msg}", file=sys.stderr, flush=True)


def _is_truthy(val) -> bool:
    if isinstance(val, bool):
        return val
    if isinstance(val, int):
        return val != 0
    return str(val).strip().lower() in ("true","yes","1")


def _parse(raw: str, transcript: str = "") -> dict:
    text = raw.strip()

    # Strip code fences
    if text.startswith("```"):
        lines = text.split("\n")
        inner = lines[1:-1] if lines[-1].strip() == "```" else lines[1:]
        text  = "\n".join(inner).strip()

    # Try direct parse; hunt for first {...} block on failure
    p: dict | None = None
    try:
        p = json.loads(text)
    except json.JSONDecodeError:
        m = re.search(r'\{[^{}]*\}', text, re.DOTALL)
        if m:
            try:
                p = json.loads(m.group())
            except json.JSONDecodeError:
                pass

    tl = transcript.lower()

    if p is None:
        _log(f"JSON parse fail: {text!r}")
        # Last-resort keyword routing
        if any(ph in tl for ph in _STOP_PHRASES):
            return {"reply_text":"Of course, goodbye!","expression":"happy",
                    "move_cmd":None,"show_text":"Bye!","should_stop":True,
                    "play_game":False,"need_outside_help":False}
        if any(ph in tl for ph in _PHOTO_PHRASES):
            return {"reply_text":"Sure, smile please!","expression":"happy",
                    "move_cmd":None,"show_text":"Say cheese!","should_stop":False,
                    "take_photo":True,"need_outside_help":False}
        if any(ph in tl for ph in _DB2_PHRASES):
            return {"reply_text":"Good IBM question — let me ask my Db2 expert agent!",
                    "expression":"surprised","move_cmd":None,"show_text":"Asking Db2 expert...",
                    "should_stop":False,"play_game":False,"need_outside_help":True,
                    "outside_kind":"db2"}
        return dict(_FALLBACK)

    move = p.get("move_cmd")
    if move in (None,"null","none",""):
        move = None
    else:
        # Case-insensitive match to an available named move; else drop it.
        move = next((n for n in _AVAILABLE_MOVES
                     if n.lower() == str(move).strip().lower()), None)

    expr = p.get("expression","normal")
    if expr not in VALID_EXPR:
        expr = "normal"

    # Honour flags from LLM output AND keyword fallback
    should_stop = _is_truthy(p.get("should_stop", False))
    if not should_stop and transcript:
        should_stop = any(ph in tl for ph in _STOP_PHRASES)

    take_photo = _is_truthy(p.get("take_photo", False))
    if not take_photo and transcript:
        take_photo = any(ph in tl for ph in _PHOTO_PHRASES)
    if take_photo:
        should_stop = False   # a photo request never ends the conversation

    need_outside_help = _is_truthy(p.get("need_outside_help", False))
    if not need_outside_help and transcript:
        need_outside_help = any(ph in tl for ph in _DB2_PHRASES)
    if need_outside_help:
        take_photo  = False        # a Db2 lookup and a photo are mutually exclusive
        should_stop = False
        # Announce the handoff; the LLM must not answer Db2 questions itself.
        return {
            "reply_text":        "Great Db2 question, let me ask my Db2 expert!",
            "expression":        "surprised",
            "move_cmd":          None,
            "show_text":         "Asking Db2 expert...",
            "should_stop":       False,
            "take_photo":        False,
            "need_outside_help": True,
        }

    return {
        "reply_text":        str(p.get("reply_text", _FALLBACK["reply_text"])),
        "expression":        expr,
        "move_cmd":          move,
        "show_text":         str(p.get("show_text",""))[:26],
        "should_stop":       should_stop,
        "take_photo":        take_photo,
        "need_outside_help": need_outside_help,
    }


# ── IAM token helper ──────────────────────────────────────────────────────────
def _get_iam_token() -> str:
    if time.time() < _token_cache["expires_at"] - 60:
        return _token_cache["token"]
    r = requests.post(
        "https://iam.cloud.ibm.com/identity/token",
        data={"grant_type":"urn:ibm:params:oauth:grant-type:apikey","apikey":LLM_API_KEY},
        headers={"Content-Type":"application/x-www-form-urlencoded"},
        timeout=15,
    )
    r.raise_for_status()
    d = r.json()
    _token_cache["token"]      = d["access_token"]
    _token_cache["expires_at"] = time.time() + d.get("expires_in", 3600)
    return _token_cache["token"]


# ── Stage-1 backends (plan_response) ─────────────────────────────────────────
def _ollama(messages: list[dict], transcript: str = "") -> dict:
    msgs = [{"role":"system","content":_system()}] + messages[-10:]
    t0 = time.time()
    try:
        r = requests.post(
            f"{OLLAMA_BASE_URL}/api/chat",
            json={
                "model":   OLLAMA_MODEL,
                "messages": msgs,
                "stream":  False,
                "format":  "json",
                "options": {"temperature":0.4,"num_predict":200},
            },
            timeout=20,
        )
        r.raise_for_status()
    except (requests.exceptions.ConnectionError, requests.exceptions.Timeout):
        _log("Ollama unreachable — falling back to Watsonx")
        return _watsonx(messages, transcript)
    raw = r.json().get("message",{}).get("content","").strip()
    _log(f"Ollama {time.time()-t0:.2f}s")
    return _parse(raw, transcript)


def _watsonx(messages: list[dict], transcript: str = "") -> dict:
    if not LLM_API_KEY:
        _log("Watsonx: no LLM_API_KEY — using keyword routing only")
        return _parse("", transcript)   # still runs keyword fallback
    msgs = [{"role":"system","content":_system()}] + messages[-10:]
    t0 = time.time()
    try:
        token = _get_iam_token()
        r = requests.post(
            f"{LLM_BASE_URL}/ml/v1/text/chat?version=2023-05-29",
            headers={"Authorization":f"Bearer {token}","Content-Type":"application/json"},
            json={"model_id":LLM_MODEL,"project_id":PROJECT_ID,"messages":msgs,
                  "parameters":{"max_new_tokens":200,"temperature":0.4}},
            timeout=30,
        )
        r.raise_for_status()
    except Exception as exc:
        _log(f"Watsonx error: {exc} — using keyword routing only")
        return _parse("", transcript)   # still runs keyword fallback
    d = r.json()
    _log(f"Watsonx {time.time()-t0:.2f}s  raw keys={list(d.keys())}")
    choices = d.get("choices") or []
    raw = choices[0].get("message",{}).get("content","").strip() if choices else ""
    if not raw:
        results = d.get("results") or []
        raw = results[0].get("generated_text","").strip() if results else ""
    _log(f"Watsonx raw reply: {raw[:120]!r}")
    return _parse(raw, transcript)


# ── Stage-2 backend (ask_outside — expert agents via WatsonX gpt-oss) ─────────
def ask_outside(question: str, kind: str = "db2") -> str:
    """
    Ask a WatsonX expert agent a free-form question and return a short plain-text
    answer ready to be spoken. kind="db2" uses the IBM Db2z expert prompt;
    kind="internet" uses a general-knowledge prompt. Both run on OUTSIDE_MODEL
    (gpt-oss). Falls back to a polite error string on any failure.
    """
    agent = "Db2 expert" if kind == "db2" else "internet agent"
    if not LLM_API_KEY:
        _log(f"ask_outside: no LLM_API_KEY — cannot reach {agent}")
        return f"Sorry, I can't reach my {agent} right now."

    try:
        token = _get_iam_token()
    except Exception as exc:
        _log(f"ask_outside: IAM token error: {exc}")
        return f"Sorry, authentication failed for my {agent}."

    system = _DB2Z_SYSTEM_PROMPT if kind == "db2" else _GENERAL_SYSTEM_PROMPT
    msgs = [
        {"role":"system","content":system},
        {"role":"user",  "content": question},
    ]

    t0 = time.time()
    try:
        r = requests.post(
            f"{LLM_BASE_URL}/ml/v1/text/chat?version=2023-05-29",
            headers={"Authorization":f"Bearer {token}","Content-Type":"application/json"},
            json={
                "model_id":   OUTSIDE_MODEL,
                "project_id": PROJECT_ID,
                "messages":   msgs,
                "parameters": {"max_new_tokens":120,"temperature":0.2},
            },
            timeout=40,
        )
        r.raise_for_status()
    except Exception as exc:
        _log(f"ask_outside error: {exc}")
        return "Sorry, the Db2 expert service is unavailable right now."

    d = r.json()
    _log(f"ask_outside: WatsonX responded in {time.time()-t0:.2f}s")

    choices = d.get("choices") or []
    if choices:
        answer = choices[0].get("message",{}).get("content","").strip()
        if answer:
            return answer
    results = d.get("results") or []
    if results:
        answer = results[0].get("generated_text","").strip()
        if answer:
            return answer

    return "I got a response but could not read the Db2 answer."


# ── Public API ────────────────────────────────────────────────────────────────
def plan_response(messages: list[dict], transcript: str = "") -> dict:
    """
    Stage-1: send conversation history to the local LLM and get a structured
    robot action plan.  Returns need_outside_help=True when the question is
    about IBM Db2 / mainframe / database topics.
    """
    return _ollama(messages, transcript) if USE_OLLAMA else _watsonx(messages, transcript)
