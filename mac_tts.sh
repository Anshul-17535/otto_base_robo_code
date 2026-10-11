#!/usr/bin/env bash
# mac_tts.sh — Mac TTS → Bob's robot speaker
#
# Uses macOS `say` (Siri-quality neural voices, Hindi-capable) to synthesise
# speech, converts it to 16 kHz 16-bit mono WAV with ffmpeg, then POSTs the
# raw WAV to the robot's /mactts endpoint which plays it at full volume.
#
# USAGE:
#   ./mac_tts.sh "Hello, I am Bob the robot!"
#   ./mac_tts.sh "नमस्ते, मैं बॉब हूँ"          # Hindi works with Siri voices
#   OTTO_IP=192.168.1.42 ./mac_tts.sh "Hi there"
#
# REQUIREMENTS:
#   • macOS 12+ (Monterey) for the good Siri neural voices
#   • ffmpeg  — install once:  brew install ffmpeg
#   • Robot on same WiFi as this Mac (or use the fallback AP at 192.168.4.1)
#
# VOICE SELECTION (change VOICE below):
#   Samantha   — US English, clear & natural (default)
#   Karen      — Australian English, very clear
#   Daniel     — British English
#   Moana      — Hawaiian / Pacific accent, warm
#   Rishi      — Indian English — great for Hinglish
#   For pure Hindi:
#     Lekha    — Hindi female  (System Prefs > Accessibility > Spoken Content)
#     Kanya    — Hindi female  (newer macOS)
#   List all installed voices:  say -v '?'
#
# TIPS:
#   • For Hindi text use "Lekha" or "Kanya" voice; Samantha reads Devanagari
#     script as gibberish.  Hinglish (Hindi written in Latin letters) works
#     well with Rishi.
#   • The robot plays at full volume.  Lower the Max98357A hardware volume
#     by adjusting the gain resistor on the amp board if it's too loud.
# ──────────────────────────────────────────────────────────────────────────────

set -euo pipefail

# ── CONFIG — edit these to match your setup ──────────────────────────────────
OTTO_IP="${OTTO_IP:-192.168.4.1}"   # default = fallback AP; set env var to override
VOICE="${VOICE:-Samantha}"          # macOS voice name (see list above)
RATE="${RATE:-22050}"               # sample rate sent to robot (22050 sounds best)
# ─────────────────────────────────────────────────────────────────────────────

TEXT="${1:-}"
if [[ -z "$TEXT" ]]; then
  echo "Usage: $0 \"text to speak\""
  echo "       OTTO_IP=<robot-ip> VOICE=Lekha $0 \"नमस्ते\""
  exit 1
fi

# Check dependencies
if ! command -v ffmpeg &>/dev/null; then
  echo "ERROR: ffmpeg not found.  Install it:  brew install ffmpeg"
  exit 1
fi
if ! command -v say &>/dev/null; then
  echo "ERROR: 'say' not found — this script requires macOS."
  exit 1
fi

TMPDIR_LOCAL="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_LOCAL"' EXIT

AIFF_FILE="$TMPDIR_LOCAL/tts.aiff"
WAV_FILE="$TMPDIR_LOCAL/tts.wav"

echo "🎙  Voice   : $VOICE"
echo "📝  Text    : $TEXT"
echo "🤖  Robot   : http://$OTTO_IP/mactts"
echo ""

# Step 1: macOS say → AIFF  (say only writes AIFF natively)
say -v "$VOICE" -o "$AIFF_FILE" "$TEXT"

# Step 2: AIFF → 16-bit mono WAV at chosen rate via ffmpeg
#   volume=50dB   : blast the level up
#   alimiter      : hard-clip at 0.9999 so every sample hits ±32767 (true max)
#   This is the same pipeline the MCP server uses for streamplay.
ffmpeg -y -loglevel error \
  -i "$AIFF_FILE" \
  -ar "$RATE" \
  -ac 1 \
  -af "volume=50dB,alimiter=level_in=1:level_out=1:limit=0.9999:attack=5:release=50" \
  -acodec pcm_s16le \
  "$WAV_FILE"

WAV_SIZE="$(wc -c < "$WAV_FILE" | tr -d ' ')"
DURATION="$(echo "scale=1; ($WAV_SIZE - 44) / (2 * $RATE)" | bc)"
echo "📦  WAV     : ${WAV_SIZE} bytes  (~${DURATION} s at ${RATE} Hz)"

# Step 3: POST the WAV to the robot as multipart/form-data
#   The ESP32 WebServer upload API only fires for multipart bodies — plain
#   --data-binary with Content-Type:audio/wav never triggers the upload handler.
#   -F "f=@file" makes curl send multipart; the robot reads it via HTTPUpload.
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
  --max-time 30 \
  -F "f=@$WAV_FILE;type=audio/wav" \
  "http://$OTTO_IP/mactts")

if [[ "$HTTP_CODE" == "200" ]]; then
  echo "✅  Sent!   HTTP $HTTP_CODE — playing on robot now."
else
  echo "❌  Failed! HTTP $HTTP_CODE"
  echo "    Is the robot reachable?  ping $OTTO_IP"
  exit 1
fi
