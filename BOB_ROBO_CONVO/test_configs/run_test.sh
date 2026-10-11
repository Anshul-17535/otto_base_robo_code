#!/usr/bin/env bash
# BOB Conversation Test Launcher
# Usage:  ./test_configs/run_test.sh <config_number>
# Configs 1-5 are described below. The server runs on port 8090.
# Open http://localhost:8090 to interact, or switch to convo mode via:
#   curl -X POST http://localhost:8090/mode/convo
#
# ROBOT must be live at 192.168.1.15 before running any config.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail
cd "$(dirname "$0")/.."

CONFIG="${1:-}"
if [[ -z "$CONFIG" ]]; then
    echo ""
    echo "┌─────────────────────────────────────────────────────────────────────┐"
    echo "│          BOB Conversation Test Configurations                       │"
    echo "├────┬─────────────┬────────────┬─────────────────────────────────────┤"
    echo "│ #  │ TTS mode    │ Mic source │ Notes                               │"
    echo "├────┼─────────────┼────────────┼─────────────────────────────────────┤"
    echo "│ 1  │ buffer      │ robot mic  │ Baseline. Bob hears you on his mic. │"
    echo "│    │             │ (SPH0645)  │ Moves OFF. Pure voice quality test. │"
    echo "├────┼─────────────┼────────────┼─────────────────────────────────────┤"
    echo "│ 2  │ stream      │ robot mic  │ Lower latency TTS (audio starts     │"
    echo "│    │ (low-lat.)  │ (SPH0645)  │ faster). Needs solid WiFi.          │"
    echo "├────┼─────────────┼────────────┼─────────────────────────────────────┤"
    echo "│ 3  │ buffer      │ Mac mic    │ Fast VAD stops recording when you   │"
    echo "│    │             │ (fast VAD) │ finish talking. Snappier turns.     │"
    echo "├────┼─────────────┼────────────┼─────────────────────────────────────┤"
    echo "│ 4  │ stream      │ Mac mic    │ Stream TTS + Mac mic. Best latency  │"
    echo "│    │ (low-lat.)  │ (fast VAD) │ combo. Moves ON (Bob dances/reacts).│"
    echo "├────┼─────────────┼────────────┼─────────────────────────────────────┤"
    echo "│ 5  │ buffer      │ Mac mic    │ Same as 3 but also plays TTS on     │"
    echo "│    │ + local     │ (fast VAD) │ Mac speaker — hear it without robot.│"
    echo "└────┴─────────────┴────────────┴─────────────────────────────────────┘"
    echo ""
    echo "Usage:  ./test_configs/run_test.sh <1|2|3|4|5>"
    echo ""
    exit 0
fi

# ── Base env (loaded from .env; these overrides come on top) ──────────────────
export ROBOT_HOST="192.168.1.15"

case "$CONFIG" in
  1)
    echo ""
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║  TEST CONFIG 1 — Buffer TTS + Robot Mic (SPH0645) + Moves OFF   ║"
    echo "║  Baseline quality test. Speak into Bob's microphone.            ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    export TTS_MODE=buffer
    export MIC_SOURCE=robot
    export WAKE_MIC_SOURCE=robot
    export CONVO_MIC_SOURCE=robot
    export CONVO_MOVE_ENABLED=0
    export TTS_PLAY_LOCALLY=0
    ;;
  2)
    echo ""
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║  TEST CONFIG 2 — Stream TTS + Robot Mic (low-latency)           ║"
    echo "║  Audio starts playing ~100ms after render (needs solid WiFi).   ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    export TTS_MODE=stream
    export MIC_SOURCE=robot
    export WAKE_MIC_SOURCE=robot
    export CONVO_MIC_SOURCE=robot
    export CONVO_MOVE_ENABLED=0
    export TTS_PLAY_LOCALLY=0
    ;;
  3)
    echo ""
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║  TEST CONFIG 3 — Buffer TTS + Mac Mic (fast VAD)                ║"
    echo "║  Recording stops the instant you stop talking. Snappy turns.    ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    export TTS_MODE=buffer
    export MIC_SOURCE=mac
    export WAKE_MIC_SOURCE=robot
    export CONVO_MIC_SOURCE=mac
    export CONVO_MOVE_ENABLED=0
    export TTS_PLAY_LOCALLY=0
    ;;
  4)
    echo ""
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║  TEST CONFIG 4 — Stream TTS + Mac Mic + Moves ON                ║"
    echo "║  Best latency combo. Bob gestures + dances during conversation. ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    export TTS_MODE=stream
    export MIC_SOURCE=mac
    export WAKE_MIC_SOURCE=robot
    export CONVO_MIC_SOURCE=mac
    export CONVO_MOVE_ENABLED=1
    export TTS_PLAY_LOCALLY=0
    ;;
  5)
    echo ""
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║  TEST CONFIG 5 — Buffer TTS + Mac Mic + Local Playback ON       ║"
    echo "║  Hear Bob's reply on Mac speaker AND robot speaker.             ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    export TTS_MODE=buffer
    export MIC_SOURCE=mac
    export WAKE_MIC_SOURCE=robot
    export CONVO_MIC_SOURCE=mac
    export CONVO_MOVE_ENABLED=0
    export TTS_PLAY_LOCALLY=1
    ;;
  *)
    echo "Unknown config: $CONFIG. Run without arguments to see options."
    exit 1
    ;;
esac

echo ""
echo "  Robot : http://$ROBOT_HOST"
echo "  Dashboard: http://localhost:8090"
echo "  TTS_MODE=$TTS_MODE | Convo Mic=$CONVO_MIC_SOURCE | Moves=$CONVO_MOVE_ENABLED"
echo ""
echo "  Quick test commands (in another terminal):"
echo "    curl -X POST http://localhost:8090/mode/convo    ← start conversation"
echo "    curl -X POST http://localhost:8090/mode/idle     ← back to idle"
echo "    curl http://localhost:8090/health"
echo ""
echo "Starting server… (Ctrl+C to stop)"
echo ""

source .venv/bin/activate
exec python server.py
