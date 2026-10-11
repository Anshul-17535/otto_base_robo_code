# BOB project handoff

Last updated: 2026-10-11. Read this first; it summarizes current state, not a complete history.

## Latest UI / eyes demo update (2026-10-03)

User tested the earlier UI and reported reversed rotation and sluggish cube
tracking. Current revision adds independent **Reverse pitch / Reverse roll**
buttons persisted in browser localStorage. They affect visualization only.
Cube now uses a critically damped spring with continuous velocity, a smaller
0.12-degree deadband and Balanced default (0.20 s parameter; Cinematic 0.32,
Responsive 0.10). Median-of-three and shortest-angle unwrapping remain. Displayed
pitch/roll animate with the cube; raw accel/gyro diagnostics remain unchanged.
Cyan FRONT / amber BACK, camera switch and recording view remain.

TFT `/eyes/demo` now shares actual normal black eye/highlight and mouth drawing
helpers. The 11-second simulation smoothly slides left/right, recenters, shakes
with an envelope, then scowls with gritted teeth and final “HEY! Easy on me!”.
The mouth follows with a slight delay and banks with the face; shake uses an O
mouth. Removed directional/intermediate captions and colored replacement eyes.
Quintic easing starts/stops gently; frame ceiling ~30 fps with 32 KB canvas.
No motor commands, speech, sensor/control or wiring changes. Existing stop and
screen ownership behavior preserved. web_gz.h regenerated.

Compiled: 1,301,674 bytes flash (41%), 69,468 bytes static RAM (21%). Actual UI
JavaScript tests passed for spikes, deadband, invalid input, wraparound,
60/120 fps equivalence and independent reversal. Syntax checks passed.
**This revision is not uploaded or visually verified on hardware.** Reflash,
then hard-refresh the robot dashboard. Try Reverse roll for the reported
clockwise/anticlockwise mismatch; adjust pitch independently if needed.
[Detailed guide](BOB_ROBO_CONVO/IMU_EYES_GUIDE.md). Python `/imu-view` also has
matching spring smoothing and persistent reversal controls.

## Current task and user preferences

- User authorized live small tabletop tests; **do not request wiring/readiness permission again**. No forward walking or large travel. User confirmed RF31 as an intended earlier idle; after restart the actual commands became **[80,65,90,45]**, with calibrated IMU. Preserve the current pose instead of assuming all joints 90° or reverting to RF31.
- A live legacy direct-servo command trial sent RF45→46→45 and robot telemetry echoed those commands. **User reports no visible robot movement. There is no shaft feedback or visual verification; only the command path was exercised.** Trial failed final settling due to a repeated IMU spike; no PID tuning is established. Do not claim the robot physically moved or that the trial passed.
- Save movement files and test evidence for later BOB_ROBO_CONVO use; integration remains deferred. Software API v2 custom-idle support is prepared but not uploaded. Running firmware still has 60–120° limits and no apiVersion field; cannot arm or save those low-RF sequences through Studio yet.
- Maintain concise Markdown notes; preserve wiring and user-saved GPIO roles. Next: inspect sensor/I²C instability, deploy new firmware with upload wiring precautions, adopt the observed correct idle without moving, then verify visible small movements with the user before selecting PID gains.

## Hardware and authoritative wiring

ESP32-S3-N16R8: 16 MB flash, 8 MB octal PSRAM. Active flags BOB_VISUAL_BRINGUP, BOB_MIC_TEST, BOB_SPEAKER_TEST, BOB_IMU_TEST are all 1. Do not switch to the legacy full-system profile.

| Peripheral | Active GPIOs |
|---|---|
| Servos, ordered LH/LF/RH/RF | Live saved map 38 / 40 / 39 / 41; factory default 40 / 38 / 39 / 41 |
| ST7735 TFT | MOSI1, SCK2, CS21, DC42, RST47 |
| OV2640 camera | 4–13 and 15–18; detailed signals in cam_pins_s3.h |
| SPH0645 mic, I2S1 | BCLK43 (TX), WS44 (RX), DOUT48; SEL to GND |
| MAX98357A amp, I2S0 | BCLK3, LRC45, DIN14 |
| MPU-6500/9250 | SDA46, SCL0; 3.3 V, AD0 to GND; CS/NCS high if present |
| Reserved | Native USB19/20; flash/OPI PSRAM26–37 |

Critical: GPIO14 belongs to the amplifier. Earlier SDA14 / speaker DIN46 proposal was superseded. GPIO0/46 are the only unassigned pair while preserving all existing connections, but are boot straps. Disconnect BOTH MPU signal wires with power off before upload; reconnect with power off afterward. GPIO46 must be low/floating for ROM download; I2C pull-up conflicts with this. Normal boot needs GPIO0 high. Amp GPIO3/45 inputs must preserve reset straps.

User previously hit wrong boot mode 0x4. Recovery: remove MPU signals, hold BOOT, press/release EN, release BOOT, reselect USB port, upload. Earlier pre-banner resets were associated with old servo/memory pin wiring; do not reuse GPIO33–37.

Hardware reported by user: one SG92R (called “923”) and three MG90S, all four working after replacing faulty motors. Which joint has SG92R is unknown. Screen works. Camera, current MPU integration and full combined circuit still require verification. Do not infer physical success from compilation.

Full wiring: [BOB_V2/WIRING.md](BOB_V2/WIRING.md). Root wiring docs and BOB_V2/archive describe older hardware.

## Implemented software

- Firmware: BOB_V2/Otto_ESP32S3/Otto_ESP32S3.ino. TFT green terminal boot intro, BOB, WiFi/IP splash and reconnection updates. Camera/mic/speaker initialize at boot; camera stream starts on request. Persistent audio clip storage remains disabled; short WAV recordings use RAM.
- **Tab-based SPA UI (html_page.h, 2026-09-28):** Dashboard rebuilt as a 6-tab single-page app to fix slow page load. All content now loads from one HTML payload but only the active tab is rendered visible. Tabs: **Move** (D-pad, expressions, saved/library moves, ticker, IMU orientation, dances), **Motors** (servo sliders, trim, assembly, GPIO pin map), **Speaker** (SAM TTS, Mac TTS, audio record/clips, diagnostics), **IMU** (3D cube, calibration, lift detect, raw accel/gyro — polls only when tab first opened), **Camera** (stream + snapshot), **System** (hardware status, WiFi, device log). Active tab persisted in localStorage. Compiled: 44% flash, 21% RAM. No endpoint changes.
- **Servo GPIO Pin Map UI** added to main dashboard (`html_page.h`): a **Servo GPIO Pin Map** card appears below the Servo Trim Calibration card. It fetches `/servomap` (GET), displays a dropdown per joint (LH/LF/RH/RF) from the allowed GPIO list, validates no duplicates, and POSTs new assignments to `/servomap`. The allowed list is centralized in `servo_mapping.h` (`ALLOWED_PINS = {38,39,40,41}`); both `handleServoMapSave` and the UI dropdown use the same list. Servo roles swap live on the ESP32 without reflashing; layout is persisted in NVS `bobservo` namespace.
- MPU raw Wire driver probes 0x68/0x69, WHO_AM_I 0x70/0x71/0x73. Accel/gyro only; no magnetometer/yaw fusion. Existing display units retained: 981 per g, 16 per degree/second. IMU endpoints and dashboard enabled; calibration required each boot. Offline retries every 2 seconds. Pin collisions checked in validate_visual_pins.h.
- BOB_ROBO_CONVO supports Bob SPH0645 or Mac mic selection, RAM /record capture and multipart /mactts playback. IMU polling defaults enabled, overridable with ROBOT_IMU_ENABLED=0. See its README.md.
- Motion Studio at /motion, linked from dashboard. Files: motion_engine.h (portable control logic), motion_api.h (HTTP/NVS integration), motion_page.h (UI). Four-joint pose preview, editable timed keyframes (max 16), six NVS slots, JSON import/export, PID settings, telemetry CSV/graph, inline camera stream.
- **Home-relative movements (2026-09-27):** every legacy gait, dance and IMU move now builds its offsets around `homeAngle(ch)` — the saved calibrated idle pose (`motion.config.customNeutral` / `neutral[]`) — instead of a hardcoded 90°. New helpers `homeAngle`/`setServoOffset`/`glideOffset` centralise this; `standNeutral`/`glideNeutral` follow the same pose. This is the fix for "forward/back/dance are wrong": they used to swing around 90° while Bob stands at ~[80,65,90,45]. With no saved pose `homeAngle` returns 90, so untouched robots behave exactly as before. `handleMove` no longer 409-rejects legacy gaits under a custom idle; it only blocks while a Motion Studio session is armed.
- **Save Calibrated Position (`GET /savepose`, 2026-09-27):** captures the current commanded servo pose as `customNeutral`/`neutral[]` with a per-joint travel window (`?radius`, default 30, 1–45; legacy gaits are not bounded by it), persists the profile to the `bobmotion` NVS `config` blob, then runs the IMU calibration and persists it — both in one call. Never writes a servo command. Dashboard button in the IMU card; `?nocal=1` skips the IMU step. Boot behaviour unchanged otherwise: the saved motor pose is held, the IMU baseline is still **not** auto-trusted (one-tap recalibrate per boot), and the boot splash + a dashboard banner prompt for `Calibrate Now` until `/imucal` runs this boot.
- **Posture / tilt sequences (PID-driven, 2026-09-27):** a sequence now has a `mode` — `"joints"` (default; absolute joint keyframes) or `"tilt"` (each frame is a target body orientation `"tilt":[pitch,roll]`). In tilt mode `base` stays at neutral and the four motors move only through PID correction, so the controller drives the IMU to each target — motions authored in orientation space, robust to servo calibration. Firmware requires PID enabled to play a tilt sequence (else 409). Targets validated within ±15° and the tilt cutoff; frames advance on time (orientation convergence depends on the physical loop). `/motion/state` reports the active `seqMode`. Engine logic covered by the offline C++ test. Motion Studio has a "Movement type" selector, posture templates, and mode-aware import/export/save/play; the dashboard/state pipeline is unchanged for joint moves.
- **Expression movement library (2026-09-27):** `BOB_V2/tools/prepare_expression_library.py --neutral LH LF RH RF [--radius --amplitude --tilt-amplitude --cutoff] --output DIR` generates a validated, idle-tagged library of 10 joint + 6 tilt movements as importable JSON (regenerate for the current saved neutral). Current set in `BOB_V2/movements/expression_library/` was generated for neutral `[81,17,90,39]`. Six NVS slots only, so the set must be curated; all are locally validated but physically untested.
- PID is outer body-tilt correction, not measured joint feedback or autonomous gait learning. Separate pitch/roll P/I/D, per-joint mix/travel, slew and correction limits. Starts off; calibration and explicit enable required. Servo command updates up to 50 Hz; fresh IMU data at 20 Hz. Defaults are unverified starting values; correction signs require physical testing.
- Motion Studio uses nonblocking playback and a token/3-second heartbeat. Stops hold the last PWM command, not remove power. PID stops on stale IMU >250 ms, lost calibration or excess tilt; an armed control-loop stall >250 ms stops when the loop resumes. Stop invalidates IMU calibration. Legacy blocking/audio/motor routes are rejected while armed; read-only status stays available. Legacy gait routines remain blocking outside Studio.
- Persistence: separate bobmotion NVS namespace; six versioned sequence documents and config. Does not persist armed/PID-on state. Hold timing starts when the commanded pose arrives within one degree, including slew delays.
- Later assisted access: BOB_V2/tools/motion_client.py. Default inspect command is read-only; explicit play uses heartbeat and stops on exit/deadline. HTTP schema, tuning workflow and limitations: [BOB_V2/MOTION_STUDIO.md](BOB_V2/MOTION_STUDIO.md).

## Accumulated for the next single reflash (2026-09-28, session 2c)

User re-centered the legs so **90° is idle**; home reset to `[90,90,90,90]`, symmetric limits `[50,130]`. New IP `192.168.1.2`. Batched, compiled + tested, awaiting ONE reflash:

- **IMU-decoupled joint playback:** open-loop joint sequences now run fully independent of the IMU (a frequent I²C dropout was aborting them with "IMU unavailable or stale"). IMU is required only for PID/tilt modes and acts as an optional tip-safety for joint moves only while it has good data. Engine change in `motion_engine.h` (`needImu`/`tipWatch`), play-gate change in `motion_api.h` (only tilt needs a fresh IMU). New/updated C++ tests pass.
- **Prior batched:** spike rejection (`TILT_STREAK=3`) — already reflashed and confirmed (a synchronized bounce completed cleanly at 90° home, peak ~9°).
- **IMU hardware is flaky:** frequent single-sample spikes (e.g. `mag=742`, sign-flipped axes) and multi-hundred-ms dropouts. This caps IMU/PID usefulness regardless of firmware — **joint-space movements are the practical path.** Root-cause the MPU SDA/SCL wiring if IMU control is wanted. Measured stationary: 200 polls, 0 full dropouts, **~3% magnitude-implausible spikes** (mag 582..2184 vs 996 median), ±3° pitch/roll noise. Spikes worsen during servo motion.
- **Full move library baked into firmware (batched, compiled):** `movements_library.h` (auto-generated from `BOB_V2/movements/expression_library/` — regenerate with the inline script in this session) holds all 31 joint moves as PROGMEM data. `GET /playmove?n=N` plays library move N as a smooth blocking on-device sequence (no token), `GET /movelist` returns the name/index list, and the dashboard has a **Move Library** card with a tap button per move. This is how "save all of them" is done — permanent in flash, no NVS slot limit, curl-ready for idle mode. Moves tuned this session at the widened `[38,142]` range with adaptive per-frame timing (~4 ms/°) so big swings complete. Index map n→name: 0 Breathe,1 Little nod,2 Look around,3 Sleepy rock,4 Curious,5 Curious lean,6 Peek,7 Sneaky tiptoe,8 Happy wiggle,9 Wiggle,10 Excited hop,11 Celebrate,12 Sad droop,13 Shiver,14 Hiccup,15 Double take,16 Bounce,17 Foot flap,18 Foot dance,19 Waddle,20 Cha-cha,21 Moonwalk slide,22 Leg wave,23 Robot,24 Dizzy,25 Sneeze,26 Kung fu,27 Tantrum,28 Kick,29 One leg stand,30 Walk forward.
- **Saved moves are now curl-playable + selectable in the dashboard (batched, compiled):** `GET /playslot?n=N` plays saved slot N as a smooth blocking on-device sequence — **no token/heartbeat**, so it works from a plain `curl` and from one dashboard tap. Joint-mode slots only (tilt/posture returns 409). Dashboard has a new **Saved Moves** card that lists non-empty slots (from `/motion/library`) as play buttons. **NVS slots expanded 6 → 12** (`bobmotion::SLOTS`). This is the idle-mode hook the user asked for.
  - **Idle-mode curl contract (for BOB_ROBO_CONVO):** `curl http://BOB/playslot?n=N` (saved move N), `curl "http://BOB/move?cmd=dance5"` (Happy) / `dance6` (Groove) / `dance0..4` / `fwd|bwd|left|right`, `curl http://BOB/motion/library` (list saved slots + names/modes to choose from). All plain GETs, fire-and-forget (return before the blocking playback completes). Foot dance is saved in slot 5 (curl `/playslot?n=5` after reflash).
- **Step-by-step gaits (batched, compiled):** `walkForward/Backward`, `turnLeft/Right` rewritten as glided lift→plant→pause marching (distinct steps) instead of continuous sway; home-relative, driven by the hipSwing/footTilt/stepPeriod sliders.
- **New signature dances (batched, compiled):** `danceHappy` (bounces → wiggle → twirl → pop finish) and `danceGroove` (sway → twist → leg-wave ripple) added as built-in on-device routines — smooth, eased, **no IMU dependency**, so reliable. Wired into `danceOne` cases 5/6, `dance()` round-robin (%7), `/move?cmd=dance5|dance6`, and dashboard buttons (Happy Dance, Groove). Bigger amplitudes suited to the 90° home.
- **In-firmware IMU despike (batched, compiled):** `imuReadRaw()` now applies a **median-of-3 per accel axis**, so a lone spike is outvoted before it reaches pitch/roll, calibration, tip-safety or PID (~50 ms lag). Should cut the ~3% spike rate to near zero. Validate live after reflash by re-running the stationary spike test.
- **Recommended PID actuator map for 90° home (apply after reflash; signs need a supported test):** LF→pitch, RH→roll, LH/RF≈dead → `joints=[[50,130,0,0],[50,130,1,0],[50,130,0,-1],[50,130,0,0]]`, gains 0 until tuned. The live config still carries the stale RF-based map from the old pose (a write timed out on the flaky link); reapply via `/motion/config` when convenient.
- **Authority at 90° home (noisy, IMU-limited):** LF strongest (~6.8° pitch, 4.7° roll), RH ~3° roll, LH dead, RF inconclusive (dropout). Re-centering shifted the main actuator from RF (old pose) to LF.
- **Confirmed:** all 4 servos actuate; single joints barely tilt a heavy body, but **coordinated multi-joint motion does** (synchronized bounce/flap → 9–25° body tilt). True sync requires on-device sequences, not PC-streamed `/servopos`.
- **Movement library regenerated for home 90** (`BOB_V2/movements/expression_library/`, `--radius 40 --amplitude 28`): 20 movements (14 joint + 6 tilt), all validated vs `[50,130]`. Added coordinated patterns `sync_bounce`, `foot_flap`, `waddle`, `wave4`. Recommended 6-slot set: Bounce(sync), Foot flap, Waddle, Happy wiggle, Look around, Little nod. After reflash: save the set and confirm visually.

## Latest live observations and changes (2026-09-27, session 2)

- User flashed the **first-pass** firmware (home-relative gaits, `/savepose`, boot prompt, dashboard button) but **not** the second pass (tilt sequences, Studio tilt UI, expression library) — `/motion/state` returns no `seqMode`. Reflash to enable posture motions and the library import.
- User disconnected the leg motors for bench work; no physical motion tested this session. All checks are software/API only.
- Live at 192.168.1.9: `/savepose?nocal=1&radius=30` verified working — captured the user's new pose `[81,17,90,39]` as `customNeutral` with envelope LH`[51,111]` LF`[0,47]` RH`[60,120]` RF`[9,69]`, IMU calibration preserved (level, pitch ~0.7°). Save path verified: armed via `motion_client.py` and wrote "Little nod" to slot 0 (replacing the old `tabletop` "Idle hold"). All six slots currently hold movements; the older ±1° `tabletop` set still occupies slots 1–5.
- **Physical movement confirmed (session 2b):** user reflashed the tilt build and calibrated the IMU with legs connected. Playing the `look_around` joint sequence moved the servos and the IMU tracked the motion (pitch/roll swung with the frames) — the first confirmed physical movement + IMU coupling. The robot did **not** tip.
- **IMU spike aborts movements — fixed:** playback tripped `tilt limit exceeded` on a single spurious sample (roll jumped to −107° for one reading, then the raw accel vector returned to exactly the calibration baseline). This is the documented intermittent MPU spike, not a fall. Fix: the engine now requires `TILT_STREAK=3` consecutive out-of-range samples (~150 ms) before a tilt stop, so a lone spike is ignored while a real sustained tip still stops promptly. Covered by the C++ test. **Needs another reflash.** Root-cause IMU/I²C data quality is still unresolved; the streak is a mitigation.
- Live config change persisted: `cutoff` raised 10→30 (expression moves naturally tilt the body more than 10°). Robot left parked at home `[81,17,90,39]`, calibrated. Note a tilt stop invalidates IMU calibration, so recalibrate before replaying.
- The tilt/PID closed loop is still untested (gains are 0). Do not treat saved/played movements as tuned or fully verified.

## Latest live observations and changes (2026-09-27)

- Current reachable address: 192.168.1.9. After the user restarted Bob, `/servomap` confirms saved pins [38,40,39,41], zero trims and commands **[80,65,90,45]**. Calibrated IMU normally reports ~1° pitch / −0.5° roll.
- Multiple anomalous samples show pitch −58.44° / roll −179.25°; one `/imu` record has ax=−521, ay=az=gx=gy=gz=0, magnitude521 versus rest980. Suspect sensor/I²C data quality; root cause and whether user handled the robot are not confirmed. Do not hide these spikes with tuning gains.
- Live RF trial: initial baseline attempt aborted without motor commands. Retry commanded RF45→46→45, but final settling sample hit the same spike. Follow-up telemetry confirms original [80,65,90,45] restored, disarmed and PID off. Physical movement not verified; user saw none. See [trial report](BOB_V2/movements/trials/REPORT.md) and raw JSON traces.
- **Automatic lift kicks were disabled and saved on Bob** via `/liftdetect?on=0` to prevent the old kicking routine during tabletop work. Re-enabling also recalibrates; do not silently restore it. No other live settings, movement slots, gains or calibration were changed. No firmware upload.
- Local libraries: [tabletop_80659045](BOB_V2/movements/tabletop_80659045/manifest.json) matches the latest observed pose; [tabletop_rf31](BOB_V2/movements/tabletop_rf31/manifest.json) preserves the earlier user-described pose. Six expression motions each, offsets ≤1°, return to their starting pose. All are physically untested and not installed on Bob.

## Prepared API v2 source

- Disarmed `POST /motion/setup` captures `expected` current commands with radius1–10°, checks the pose has not changed, and persists idle/limits without PWM writes. Default UI radius3°, speed5°/s, correction1°, cutoff10°, PID gains zero. Arming holds that custom pose.
- Saved `neutral` and `customNeutral` fields survive NVS with servo map/trim binding; matching profiles control startup and Stand. Pin map changes clear the active profile; mismatched saved profiles are ignored on boot. Before the first profile is saved boot still sends centre pulses; electrical assembly/calibration buttons still intentionally command90°. Support the mechanics during first upload/startup.
- Studio uses current commands on load, offers capture and smooth return-to-idle. Custom sequences require fresh calibrated IMU even with PID off; tilt/stale/lost-calibration stops apply. Legacy90° gaits and automatic lift kicks are disabled while a custom profile is active.
- Added gravity-magnitude plausibility gate (0.75–1.25g) for slow posture control; exposed `imuSampleValid` and `accelMagnitude`. This rejects the recorded521-magnitude sample. It is a control guard, **not a fix or diagnosis of the sensor fault**.
- Existing seven adjustable templates and sequence expression−1..6 remain. `prepare_tabletop_library.py` generates reusable JSON without contacting Bob. `tabletop_jog.py` runs explicit±1° legacy command trials, checks pose/IMU, leaves lift kicks disabled, and logs failure; `--observe-only` never writes hardware.

## Validation and remaining checks

Latest firmware compile passed: 1,394,518 bytes flash (44% of app partition), 69,340 bytes static RAM (21%). Built for esp32:esp32:esp32s3 with FlashSize=16M, PartitionScheme=huge_app, PSRAM=opi, UploadSpeed=921600. No upload performed by the assistant.

Passed offline: portable C++ controller tests with address/undefined sanitizers (custom idle, bounds, zero-motion arm, PID-off IMU stops and gravity plausibility); 10 Python lifecycle/library/jog-preflight tests; UI JavaScript syntax. Live evidence is listed separately above. BOB_ROBO_CONVO code unchanged.

Pending: new firmware upload, browser visual/interaction verification, API v2/NVS checks on hardware, sensor fault investigation, visible servo movement confirmation, correction-direction verification and PID tuning. Browser testing was retried but could not start: browser tool reports missing sandboxPolicy metadata. Do not claim browser or hardware validation. Temporary preview/build paths are disposable.

Useful commands, run from this workspace:

    '/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=huge_app,PSRAM=opi,UploadSpeed=921600 --build-path /tmp/bob-s3-motion-build BOB_V2/Otto_ESP32S3
    clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined BOB_V2/tests/motion_engine_test.cpp -o /tmp/bob-motion-engine-test
    /tmp/bob-motion-engine-test
    python3 -m unittest discover -s BOB_V2/tests -p 'test_*.py' -v
    BOB_ROBO_CONVO/.venv/bin/python -m unittest discover -s BOB_ROBO_CONVO/tests -v

Current robot IP: **192.168.1.15** (verified live). USB serial port no longer needed for firmware updates — robot is now on the OTA partition scheme (`app3M_fat9M_16MB`, two 3 MB slots). All future flashes use `./BOB_V2/ota_flash.sh` (WiFi OTA, no USB, no MPU disconnect). Firmware AP fallback is 192.168.4.1.

## TTS noise isolated — output-stage clipping, not DSP (2026-10-11, session 2)

**Resolved the "noise mixed with speech" to the output stage.** Listening
results from replaying the saved references and a new level sweep:

- **A/B compression replay** (both saved clips, identical 0.225 peak): user
  reported **A ≈ B, equal noise**. The compressor is NOT the cause; source
  DSP/compression is ruled out.
- **Noise character** confirmed by user: **distortion / fuzz on loud
  syllables** (clean when quiet, breaks up on louds) — classic analog
  clipping in the amp→speaker output stage.
- **Level sweep** (same current-processing phrase scaled to peaks 0.225 /
  0.150 / 0.100 / 0.060, played via :8080/mactts, all HTTP 200). User: only
  **clip 4 (peak 0.060) is fully clean** and "best". So the output stage's
  clean ceiling is ~0.06 of full scale; at the old 0.25 gain (peak 0.225) it
  was driven ~3.8× past clean → the reported fuzz.
- Sweep clips + stats saved: `BOB_ROBO_CONVO/tests/results/tts_async_20261011/noise_isolation/level_sweep/`
  (`sweep_report.json`, `peak_225/150/100/060.wav`). Generator (scratchpad):
  scales the saved `2_current_processing.wav`; no re-render, no DSP change.

**Change made:** `TTS_OUTPUT_GAIN` lowered **0.25 → 0.067** in
`BOB_ROBO_CONVO/.env` and `.env.example` — reproduces the user-approved clean
clip 4 (peak 0.060). CLEAN but **faint**; the output stage has little clean
headroom. No conversation server was running, so no restart was needed; the
next start loads it. No firmware/wiring/chunk/padding change.

**Why faint, and the open hardware question:** a clean ceiling as low as
peak 0.06 is unusually small and is consistent with a **speaker-impedance
mismatch** — the MAX98357A amp (GPIO3/45/14) is rated for 4 Ω/8 Ω, and a 2 Ω
speaker (notes list "4 W / 2 Ω", still **unconfirmed by user**) would overload
it and distort on peaks. The amp **GAIN pin** setting is also unknown. Both
are hardware items; per standing instructions, **not proposing rewiring yet** —
awaiting the user's confirmation of actual speaker impedance.

**Loudness recovery — done and live (2026-10-11, session 2).** Since clip 4 was
clean but faint, tested raising the AVERAGE level while keeping the peak at the
0.06 clean ceiling:
- Re-compressing the (already dense) waveform LOWERED RMS — dead end.
- Tanh **soft-clip with input drive**, normalized to peak 0.06, raises RMS at a
  fixed peak: drive 2/3/5/8 = +3.4/+4.8/+6.3/+7.1 dB vs clip 4. Played all via
  :8080/mactts (HTTP 200). **User picked drive 2 (+3.4 dB) as the loudest that
  stayed clean.** Clips: `.../noise_isolation/loudness/` (`0_baseline_clip4`,
  `drive2/3/5/8.wav`, `loudness_report.json`).
- **Implemented in the pipeline:** `lib/robo_tts.py` `_soft_clip_pcm()` +
  `_to_pcm()` apply tanh soft-clip after the limiter when `TTS_SOFTCLIP_DRIVE>0`,
  normalizing to `TTS_OUTPUT_PEAK`. Set in `.env`/`.env.example`:
  `TTS_SOFTCLIP_DRIVE=2.0`, `TTS_OUTPUT_PEAK=0.06` (fallback
  `TTS_OUTPUT_GAIN=0.067` used only when drive=0).
- **Validated:** helper reproduces `drive2.wav` **bit-for-bit**; `_to_pcm` live
  path → peak 0.060 / RMS 0.031; drive=0 fallback → peak 0.060 / RMS 0.021;
  7 TTS unit tests pass; one live end-to-end clip rendered through the real
  conversation path (`_to_pcm`→WAV→/mactts) played on the robot (HTTP 200).
- No conversation server was running; next start loads the new `.env`. No
  firmware/wiring/chunk/padding change.

**De-harsh + louder — done and live (2026-10-11, session 2, step-by-step).**
Variety test (6 sentences/paragraph at drive 2) surfaced two issues: sibilant
clips ("seashells") hissed, and overall too quiet "for everyone". Root cause of
the hiss: the tanh soft-clip adds high-frequency harmonics, worst on sibilants.
Fix found by stepwise listening on the robot:
- A **low-pass AFTER the soft-clip** removes those harsh harmonics (the small
  speaker can't reproduce them cleanly anyway) → sibilance clean at 4.5 kHz.
- Critically, removing the highs **raised the clean peak ceiling from ~0.06
  (full band) to ~0.10**. User stepped peak up: 0.10 = "loud and clear" on all
  sentences incl. worst-case sibilance; **0.15 distorts**. So 0.10 is the max
  clean output peak with the low-pass — a real loudness gain (RMS ~0.05 vs the
  earlier ~0.02/0.03), not just an average nudge.

**Final chain (live in `lib/robo_tts.py` `_to_pcm`):** compressor → limiter 0.9
→ tanh soft-clip (drive) → low-pass → peak-normalize. New helpers
`_lowpass_pcm` / `_normalize_peak_pcm`; order matters (low-pass changes peaks,
so normalize is last). Env (`.env` + `.env.example`):
`TTS_SOFTCLIP_DRIVE=2.0`, `TTS_POST_LOWPASS_HZ=4500`, `TTS_OUTPUT_PEAK=0.10`
(drive=0 disables the chain → plain `TTS_OUTPUT_GAIN=0.067`;
`TTS_POST_LOWPASS_HZ=0` soft-clips without the low-pass, keep peak ~0.06 then).
Validated: `_to_pcm` → peak 0.100 on greeting + seashell; live end-to-end clip
played on the robot (HTTP 200); 7 TTS unit tests pass. Candidate clips saved in
`.../noise_isolation/deharsh/` and `.../peaksweep/`. No conversation server was
running; next start loads the env. No firmware/wiring/chunk/padding change.

**Still open (hardware — not done):** even at the improved peak 0.10 ceiling the
output stage is the limit (0.15 distorts). If still not loud enough for a room,
the real fix is hardware: confirm the physical **speaker impedance** — user does
**not** know it yet (needs to check; usually printed on the speaker/magnet). If
it is 2 Ω, the MAX98357A (rated 4 Ω/8 Ω) is overloaded and a 4 Ω/8 Ω speaker —
or a lower amp GAIN-pin setting — is the real fix for genuinely loud, clean
audio. Not proposing rewiring until impedance is known.

Everything below is the prior (2026-10-11 session 1) state, retained for history.

## TTS audio pipeline — current work (2026-10-11)

**Latest user clarification:** the remaining problem is **noise mixed with
speech**, rather than stuttering/timing gaps. Lower volume helped but did not
solve it. Do not label the issue resolved from HTTP passes or the initial
clearer A/B result. Leave chunk length, padding, firmware and wiring unchanged
while separating source processing from amplifier/speaker output noise.
Noise character and actual speaker impedance have been asked; answers pending.
Three short diagnostic clips were played successfully (HTTP 200 each):
1. quiet 600 Hz sine, 1.5 s with 1 s digital silence before/after, peak 0.08;
2. current processed voice, 4.06 s, peak 0.225 / RMS 0.0784;
3. identical source phrase without compression, peak 0.225 / RMS 0.0465.
Await which clip(s) had noise and whether silence also had noise. Lower RMS
of clip 3 is a confound: improvement alone does not prove compression is the
cause. No permanent DSP, firmware, chunking, padding or wiring changes made
for this comparison. WAV references, stats and logs are saved in
`BOB_ROBO_CONVO/tests/results/tts_async_20261011/noise_isolation/`.


**Latest state:** Python now sends buffered speech to
`TTS_BASE_URL=http://192.168.1.15:8080` with HTTP failures propagated.
Bob's port-80 `/hw` was reached this session. The first build failed before
upload because sketchbook ESPAsyncWebServer 3.1.0 uses removed mbedTLS APIs
with ESP32 Arduino 3.3.11. Fresh build with project-local ESP32Async dependencies passed: 1,361,178
bytes flash (43%), 72,388 bytes static RAM (22%). The approved OTA rerun succeeded, and Bob rebooted (uptime 8 seconds at
verification). Live conversation test completed: 8/8 HTTP passes, 15/15 chunks
`played ok=1` on Core 0, no `STALL` in captured logs. **The initial full-level test still stuttered. In the subsequent A/B test,
the user confirmed the second, quarter-amplitude clip was quieter but clear.**
All clips took expected WAV duration + 5.8–9.5 ms; maximum single I2S write
was 6.818 ms. This does not support the earlier long-write/underrun diagnosis
for this run. Evidence: `BOB_ROBO_CONVO/tests/results/tts_async_20261011/firmware.log`.

Confirmed voice settings retained: Karen, macOS say, 22050 Hz, 150 wpm,
heavy compressor, limiter peak 0.9, **TTS_OUTPUT_GAIN=0.25 after the limiter**,
no highpass, 60-character chunks, 300 ms tail
silence, serial buffered mode. `.env` contains the TTS host override; restart
an already running conversation server to load it. Port-80 control routes and
GPIO wiring are unchanged. No physical movement tests authorized this session.

### Implemented and checked

- `lib/robo_tts.py`: separate `TTS_BASE_URL` (defaults to `ROBOT_BASE_URL`),
  multipart `requests.post`, `raise_for_status`. Offline mock checks passed for
  configured port, request timeout and propagated HTTP errors. Four existing
  chunking/WAV tests pass; the WAV test now uses the configured sample rate
  instead of assuming 16 kHz and checks silence padding.
- Buffer-mode synthesis/upload remains serial; the firmware returns success
  after playback. Prior-session overlap fixes are preserved.
- Pending async firmware had a multipart bug: file bytes go to `onUpload`,
  not `onBody`. Corrected to accumulate decoded file bytes in PSRAM, validate
  canonical mono PCM16 WAV, reject overflow, and release on disconnect.
- Removed the upload mutex wait: blocking the single async task would prevent
  another in-progress upload from completing. A request owner rejects overlap.
- Added hardware-ready/Motion Studio checks, checked I2S write results,
  watchdog feeds and DMA-tail drain. Logs include actual playback core,
  `ok` and maximum I2S write duration.
- `build_opt.h` pins AsyncTCP to Core 0 (the old installed version defaulted
  to either core). OTA explicitly selects project-local ESPAsyncWebServer
  3.12.1 / AsyncTCP 3.5.0. Source and licenses in `BOB_V2/libraries`; see its
  README for manual build flags. System libraries were not changed.

### Evidence limits and corrected diagnosis

The previous session described cross-core semaphore latency as a confirmed
root cause and claimed the legacy I2S DMA ISR always runs on Core 0. That is
**not established**. This firmware installs I2S from `hwInitTask`, pinned to
Core 1; Espressif documents peripheral interrupt allocation on the calling
core ([interrupt allocation](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/api-reference/system/intr_alloc.html)).
Moving playback to the async task isolates the main loop, but does not prove
same-core DMA operation or eliminate WiFi scheduling effects. No `STALL`
entries proves only that the loop-stall threshold was not exceeded, not that
audio had no underruns. HTTP success is not a listening test.

Earlier reported trials included different write sizes, yields, DMA sizes
and Core 0/1 tasks, with stutter/gaps remaining. Preserve the current voice
settings while testing this async change. Tail padding plus serial rendering
can still produce pauses **between** chunks.

### Clear output level selected and conversation test completed

User explicitly reported the second clip was quieter but clear. Applied that
level using `TTS_OUTPUT_GAIN=0.25` in `.env` and `.env.example`. `_to_pcm` applies
it after the limiter, preserving the tested waveform; pre-limiter
`TTS_GAIN_DB` is not equivalent. Offline sample comparison against quarter
amplitude passed within one PCM16 step (peak approximately 0.225).
The quieter conversation test completed: 8/8 HTTP passes, 15/15 clips
`played ok=1`, zero captured `STALL` lines, max I2S write 6752 us.
Evidence: `tests/results/tts_async_20261011/quiet_*` under BOB_ROBO_CONVO.
Audible confirmation applies to the original quarter-amplitude A/B clip;
the full quieter conversation has transport/log validation.
No firmware reflash is needed for this Python/configuration change. A/B clips were prepared and played successfully via port 8080
from the same 7.97-second utterance: current processing versus quarter
amplitude (-12 dB), each as one upload with no chunk boundaries. Script:

    cd BOB_ROBO_CONVO
    .venv/bin/python3 tests/compare_tts_audio.py        # prepare only
    .venv/bin/python3 tests/compare_tts_audio.py --play # two speaker clips

Both returned HTTP 200 (A 9.226 s, B 9.518 s including upload); firmware
playback was 8.278 s each, max write 6.831 / 7.727 ms, no new stalls.
**User confirmed B was clear; A was the louder problem level.** This suggests
level-dependent distortion, but does not isolate amplifier, speaker or power
supply as the specific cause. Keep the clearer setting; no wiring changes.
Durable measurements/logs: `tests/results/tts_async_20261011/` under
BOB_ROBO_CONVO. Default output `/tmp/bob-tts-comparison/` contains the WAVs.
Next: isolate the reported noise with a quiet generated tone and speech
without compression through the same buffered endpoint. Keep output low and
wait for the listener result before choosing another permanent DSP change.
Restart any already-running Python conversation server to load code/env changes.
Inter-chunk pauses can remain because rendering/upload is serial with 300 ms
tail padding. No new firmware changes since the successful OTA.
The comparison script is relative to the current configured level: now its
B would attenuate the already-quiet level again; do not confuse a rerun with
the original A/B evidence.

## WiFi OTA — active as of 2026-10-04

Robot was flashed via WiFi with the OTA-capable partition scheme. IMU wires do NOT need to be disconnected for WiFi OTA (only USB/ROM bootloader needed that). One-command update workflow:

    ./BOB_V2/ota_flash.sh                   # compile + push to 192.168.1.15
    ./BOB_V2/ota_flash.sh 192.168.1.XX      # if IP changes

After every OTA reboot: open the dashboard IMU tab and click **Calibrate Now** — IMU calibration does not survive a reboot.

## Movements library — rebased to saved idle [54, 71, 71, 86]

All 33 baked-in moves in `movements_library.h` were regenerated with neutral `[54,71,71,86]` as the base. Every frame now starts and returns to the robot's actual saved idle pose, not the old hardcoded `90,90,90,90`. When you re-calibrate and save a new idle pose, regenerate with:

    python3 BOB_V2/Otto_ESP32S3/tools/gen_movements_library.py \
        --neutral <LH> <LF> <RH> <RF> --amplitude 28 \
        --output BOB_V2/Otto_ESP32S3/movements_library.h
    ./BOB_V2/ota_flash.sh

Git status: this whole workspace is untracked under a parent repository, which also has unrelated user changes. Preserve those; avoid broad git add/reset/clean operations.
