# Conversation server handoff

Updated 2026-10-03. Broader hardware history: [parent handoff](../PROJECT_HANDOFF.md).
The [parent TTS analysis](../TTS_AND_REACTION_ANALYSIS.md) is historical: rendering
already pipelined before these changes; HTTP display helpers are synchronous.

Latest task: refine IMU cube tracking/direction and reuse the normal face in
scripted eyes demo. User reports previous UI looks good but rotation is reversed.
Current revision: browser-persisted Reverse pitch / Reverse roll, critically
damped smoothing (Balanced default), 0.12° deadband, animated angle readouts.
Same changes in firmware dashboard and optional Python /imu-view.
Eyes demo uses shared normal black eye/highlight and mouth helpers; mouth follows
with a delay, banks, opens during shake, then grits teeth. Removed intermediate
captions; only final angry line remains. Quintic slide, gentler shake, ~30 fps
ceiling, 11 seconds. No servo, speech or wiring changes.
Compiled: 1,301,674 bytes flash / 69,468 static RAM. JavaScript syntax and actual
firmware smoothing/direction tests pass. This revision is not uploaded or
physically verified. See [IMU_EYES_GUIDE.md](IMU_EYES_GUIDE.md).
Next: reflash, hard-refresh and use Reverse roll for reported inversion; adjust
pitch separately. Physical movement tests still await wiring readiness.

Previous speech/RPS work (preserved):
Current task: complete TFT reply text, prevent idle interference during RPS,
clear reveal artwork, reduce mid-sentence audio pauses without reflashing.
User logs showed idle combos during RPS and 11.9 s POST for 1.9 s audio. This
supports contention as a contributor, not a measured diagnosis of all delay.

Implemented in server.py, lib/robo_robot.py and lib/robo_tts.py:
- Shared reentrant foreground ownership with pending-owner priority over idle.
  Idle polling/greetings/recording/fidgets yield for complete game/speech work;
  current move drains, remaining combo cancels. Camera polling pauses.
- Full reply text on 26-character ASCII pages. First page during audio, remainder
  after audio for two seconds each; no abbreviated show_text/26-character cutoff.
  Greetings, Db2 answers and RPS intro/victory use the same display path.
- RPS holds paper briefly, explicitly changes face before victory and restores
  idle face on all exits. Removed victory dance to keep demo still.
- Removed extra 45-character first split (now follows TTS_CHUNK_CHARS; local 400).
  Existing synthesis/display overlap, producer pipeline, speech serialization,
  safe temporary-file cleanup and first-upload timing remain.

Validation: 15 offline tests pass: activity contention/handoff, complete long
text, punctuation, RPS cleanup/no moves, pipeline ordering, WAV header and API.
Configured voice/rate/filters preserved. .env robot IP is 192.168.1.8; earlier
read-only /hw confirmed reachable and I2S initialized. No new live playback,
movement test, server restart or firmware upload. No wiring changes.

Limitations: firmware blocks ticker updates during buffered I2S playback. Full
text therefore adds reading time after speech. Smooth simultaneous paging and
gapless streaming require firmware changes. Installed-firmware face cleanup and
audible continuity remain unverified. Do not promise all pauses are eliminated.
Next: restart server, verify full reply and RPS, inspect absence of idle logs
inside game and observe restored face. Wait for wiring confirmation before
assistant-run movement tests. See [README](README.md#speech-text-and-rps-ownership-2026-10-03).
