# BOB_ROBO_CONVO — ESP32-S3 conversation controller

Matches `../BOB_V2/Otto_ESP32S3` with screen, camera, four servos, SPH0645 microphone, MAX98357A speaker and MPU-6500/9250 IMU enabled. Flash clip storage remains disabled.

## Start and choose a microphone

```bash
cd BOB_ROBO_CONVO
source .venv/bin/activate
python server.py
```

Once model loading finishes, open **http://localhost:8090**. Select **Bob mic** or **Mac mic**, then click **Start conversation**. **Stop** ends the conversation after the current blocking audio operation. Repeated Start requests do not create duplicate conversation workers.

The microphone choice takes effect on the **next recording turn**. It lasts until the server restarts; `MIC_SOURCE` in `.env` supplies the startup default. Mac mic always means the Mac running this Python server, even if you open the page from a phone.

- **Bob mic:** downloads a fixed 1–3 second, 16 kHz mono PCM16 WAV from the SPH0645 through `/record`. Speak short phrases when Bob's listening screen appears. This firmware does not provide continuous streaming or early silence detection over HTTP. Increase neither the request nor `ROBOT_RECORD_SECS` beyond three seconds.
- **Mac mic:** retains local sounddevice capture with noise-floor calibration and silence detection. Choose your input in macOS Sound settings and allow microphone access for the terminal running Python.
- Replies use macOS `say`, ffmpeg and a multipart WAV upload to Bob's **`:8080/mactts`** endpoint. Set `TTS_BASE_URL=http://192.168.1.15:8080` using Bob's current IP; ordinary robot control stays on port 80. Restart the Python server after changing `.env`. Without this variable, the client falls back to `ROBOT_BASE_URL` for older firmware. Stream mode still needs port 80. The uploaded WAV has the exact 44-byte PCM header required by the firmware; the speaker uses its dedicated GPIO3/45/14 wiring.

**Upload the updated ESP32 sketch too.** Earlier speaker-test builds enabled `/testtone` but still blocked `/mactts` with HTTP 503. The current firmware allows streaming speech when `BOB_SPEAKER_TEST=1`, without enabling flash clip storage. Keep `BOB_VISUAL_BRINGUP=1` and `BOB_MIC_TEST=1`.

Buffered TTS uploads and playback are serial: HTTP success follows playback. The async server receives multipart file bytes into PSRAM, then writes PCM on its Core 0 task (`build_opt.h` pins AsyncTCP). It leaves the main loop available. Run `.venv/bin/python3 tests/test_tts_conversation.py` and inspect `/logs` for `mactts: played ok=1` and new `STALL` entries. A passing HTTP test and no loop stalls establish transport/responsiveness, not audible quality; listen for interruptions within each phrase. The 300 ms silence padding and serial rendering can still leave pauses between chunks.

**Current limitation:** the user clarified that unwanted noise remains mixed with speech, even at the quieter level. Audio quality is unresolved; clean HTTP/loop logs do not establish clean speaker output. Chunk timing is not the current diagnosis. Three noise-isolation clips (quiet sine with silence, processed speech, and uncompressed speech) have been played; listener comparison is pending. Their reference WAVs and measurements are in `tests/results/tts_async_20261011/noise_isolation/`.

Live validation (2026-10-11): OTA succeeded; the original conversation test returned 8/8 HTTP passes across 15 chunks with no loop stalls or failed I2S writes, but the user still heard stutter. In a comparison using identical samples, **the user confirmed quarter amplitude was quieter but clear**. `.env` now sets **`TTS_OUTPUT_GAIN=0.25`**, applied after the limiter to reproduce that level (approximately -12 dB, output peak ~0.225 with the current 0.9 limiter). Karen, 22050 Hz and the other voice settings are retained. Restart an already running Python server to load the new code and settings; no firmware upload is needed. The quieter conversation rerun completed with 8/8 HTTP passes, 15 successful clips and no captured loop stalls.

`tests/compare_tts_audio.py` prepares the current configured output and a quarter-amplitude variant of it; `--play` sends both to Bob. With the new setting, its second clip would be quieter again. Original A/B evidence is in `tests/results/tts_async_20261011/`. Level-dependent distortion is suggested; the specific hardware cause has not been isolated.

## First-time setup

```bash
brew install ffmpeg portaudio
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
# Only if you do not already have a configured .env:
cp .env.example .env
ollama pull qwen2.5:7b
python server.py
```

Set `ROBOT_HOST` to the home IP displayed on Anvyxo's TFT (verified `192.168.1.8` on 2026-10-03). `anvyxo-robo.local` or a complete `http://...` URL also works. The camera uses this same host at `/cam/snapshot`; no separate camera board or `CAM_HOST` is needed. Robot recording/playback and display requests are serialized because the ESP32's control web server handles audio synchronously.

For rock-paper-scissors, run `./setup_gesture.sh` once to prepare the separate MediaPipe worker. Whisper, Ollama planning, expressions, movements, ticker text, camera snapshots and the existing RPS game remain part of the conversation loop.

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `ROBOT_HOST` | `192.168.1.12` | ESP32-S3 address |
| `MIC_SOURCE` | `robot` | Startup mic: `robot` or `mac`; change live in UI |
| `ROBOT_RECORD_SECS` | `3` | Bob capture length, clamped to 1–3 seconds |
| `ROBOT_IMU_ENABLED` | `1` | Poll the MPU-6500/9250 lift state; set to 0 to disable IMU-driven reactions |
| `TTS_VOICE` | `Samantha` | Installed macOS say voice |
| `TTS_GAIN_DB` | `0` | Speech gain before limiting; replaces the old fixed 50 dB boost |
| `TTS_PLAY_LOCALLY` | `0` | Also play the reply on the Mac |
| `FFMPEG_PATH` | `/opt/homebrew/bin/ffmpeg` | ffmpeg executable |
| `WHISPER_MODEL` | `distil-small.en` | Local speech-recognition model |
| `OLLAMA_BASE_URL` | `http://localhost:11434` | Ollama server |
| `OLLAMA_MODEL` | `qwen2.5:7b` | Local planning model |
| `LISTEN_DURATION` | `10` | Mac mic maximum duration |
| `SILENCE_AFTER_SPEECH_MS` | `800` | Mac mic silence cutoff |
| `VAD_THRESHOLD_MULT` | `3.0` | Mac mic noise-floor multiplier |
| `ROBO_CONVO_PORT` | `8090` | Control page and HTTP API port |
| `CONVO_MAX_TURNS` | `20` | Maximum turns before returning to idle |
| `IDLE_ANIM_ENABLED` | `1` | Play random built-in moves while idling; `0` disables |
| `IDLE_ANIM_MIN_GAP` | `6` | Min seconds between idle moves |
| `IDLE_ANIM_MAX_GAP` | `18` | Max seconds between idle moves |

While idling, Bob also plays a random built-in move every `IDLE_ANIM_MIN_GAP`–`IDLE_ANIM_MAX_GAP` seconds so he looks alive; gentle fidgets (nod, look around, curious) are frequent and big/showy or locomotion moves are rare. Moves are resolved by name from the robot's `/movelist`, so reflashing new moves (e.g. Turn left/right) makes them eligible automatically. The idle screen polls lift state with the MPU-enabled ESP32-S3 firmware. Scared/shake reactions require `ROBOT_IMU_ENABLED=1`, a connected IMU, and a fresh calibration after each robot power-on. Watsonx fallback settings remain in `.env.example`.

## API and checks

```bash
curl http://localhost:8090/health
curl -X POST http://localhost:8090/mic -H 'Content-Type: application/json' -d '{"source":"mac"}'
curl -X POST http://localhost:8090/mic -H 'Content-Type: application/json' -d '{"source":"robot"}'
curl -X POST http://localhost:8090/mode/convo
curl -X POST http://localhost:8090/mode/idle
python -m unittest discover -s tests -v
```

Tests mock hardware, microphone and speech output. Physical mic capture, amplifier playback and the complete Whisper/LLM conversation still require testing on Bob after upload. If TTS returns 503, upload the updated sketch and inspect `/hw` and `/logs` at the robot's IP. For wiring and boot-pin notes see `../BOB_V2/WIRING.md`.

## Speech, text and RPS ownership (2026-10-03)

Restart `python server.py` to load these host changes. No reflash is required.
`ROBOT_HOST=192.168.1.8` was verified by read-only `/hw` earlier this session.

Conversation replies, greeting, Db2 answers and the RPS introduction/victory use
the complete spoken text, not the LLM's abbreviated `show_text`. The TFT shows
26-character word-wrapped ASCII pages, including text beyond the firmware's
four-page limit. The first page appears during speech; remaining pages appear
**after speech**, two seconds each, before the next listening turn. This adds
reading time for long replies. Firmware synchronous I2S playback does not run
ticker animation; smooth simultaneous paging/audio requires firmware work.
Unicode punctuation is normalized to avoid garbage glyphs on the TFT font.

A shared activity guard excludes idle polling, greetings, wake recording and
fidgets for the entire RPS round and each speech/text interaction. A pending
interaction lets an existing idle move finish before taking ownership and
prevents the rest of its combo from starting. Camera polling pauses too.
The RPS demo has no victory dance; it holds paper briefly, explicitly changes
face to clear the artwork, and restores the idle face even on cancellation or
failure. Visual cleanup still needs verification against installed firmware.

TTS still renders subsequent chunks while uploading/playing the previous one.
The extra 45-character opening split has been removed: `TTS_FIRST_CHUNK_CHARS`
now defaults to `TTS_CHUNK_CHARS` (local setting 400), so ordinary replies use
one buffered WAV. An explicit smaller opening override remains supported.
Whole utterances are serialized; temporary files survive until rendering stops.
Existing voice, 8 kHz local sample rate and filters are preserved. Logs measure
host preparation and complete POST duration, not actual audible onset. Remaining
long replies still have upload boundaries; gapless streaming requires firmware.

Validation: 15 offline tests pass, including ownership handoff, complete text
paging, RPS cleanup, pipeline ordering and WAV format. No live movement/audio
test, server restart or firmware upload was performed for this update.
Next: restart the server and test one full reply and RPS; check that no idle
fidget log occurs inside the round and verify speaker continuity / face cleanup.

## IMU recording and liquid eyes

Open **Motion studio** in this Python dashboard for the optional smoothed,
labeled recording view. The firmware dashboard also has an upgraded cube and
Play/Stop buttons for a scripted TFT eyes demo; those require reflashing.
See [IMU_EYES_GUIDE.md](IMU_EYES_GUIDE.md) for usage, validation and limitations.
