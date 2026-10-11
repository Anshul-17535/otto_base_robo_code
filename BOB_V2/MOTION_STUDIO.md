# BOB Motion Studio

Open http://BOB-IP/motion after flashing, or use **Open Motion Studio** on the robot dashboard. No GPIO assignments change for this feature. The [wiring guide](WIRING.md) still applies, including disconnecting MPU SDA46/SCL0 while uploading.

## What you can do

- Set all four logical joint angles, preview a pose, then add it to a timeline.
- Edit/reorder up to 16 keyframes. Each has four angles, a 100–5000 ms move time, and a 0–5000 ms hold time. Playback uses smooth transitions, respects speed limits, and can repeat 1–10 times.
- Save six named movements in ESP32 NVS, load them after a restart, or export/import JSON. Saving replaces the selected slot. Saved moves never start automatically.
- Tune separate pitch and roll P/I/D gains, target tilt, per-joint travel, correction direction/mix, speed, and correction magnitude. Apply changes live; use **Save settings on Bob** to persist them.
- Watch tilt, commanded angles, correction, limit activity and a rolling graph. Download up to 2400 telemetry rows as CSV. RMS/peak statistics accumulate only while PID is active in the current armed session.
- View the camera stream on the same page without leaving the control session.

Joint order everywhere is **left hip, left foot, right hip, right foot**. Default GPIOs are 40/38/39/41; the live saved mapping read on 2026-09-27 is **38/40/39/41**. Studio displays `/servomap`; preserve the saved mapping. Angles are command values before the existing servo trims. The SG92R and MG90S servos do not send measured position feedback.

## Starter movements and expressions

The updated source adds seven editable templates: Gentle sway, Happy bounce, Curious lean, Little bow, Sleepy rock, Twist dance, and Step-touch study. Amplitude (1–15°) and tempo (30–120 BPM) generate offsets around the current pose sliders. Every template starts and ends at that pose. Tempo sets transition time; holds and slew limits extend total duration. These are unverified command patterns, not proven walking gaits. Loading a template only edits the timeline and never arms, plays, saves, or enables PID. It replaces the editor contents; export existing work first.

Choose a face for the sequence: keep current, happy, sad, angry, surprised, sleepy, love, or normal. Optional JSON field `expression` is −1 (keep current, the default for old files) or 0–6 in that order. Firmware applies the face when play is accepted; it remains afterward. Saved slots and JSON exports retain this field. Older firmware ignores it, so upload the updated build to use sequence expressions.

## Current assembly and tabletop profile

The user confirmed **[90,90,90,31]** as an earlier assembled idle pose; RF31 was intentional. After restart, live commands changed to **[80,65,90,45]**, with calibrated IMU normally near 1° pitch/−0.5° roll. Use the actual verified standing pose rather than forcing either reference. Small tabletop tests are authorized; no forward walking.

Live testing sent RF45→46→45; status echoed the requested values. The user saw no visible movement. This confirms command handling only, not physical servo travel. The final settling check failed on an IMU spike (−58.44° pitch/−179.25° roll); read-only recording also captured ax −521 with other axes zero and magnitude 521 versus rest 980. PID remains off. **Automatic lift kicks were disabled and saved on Bob**; do not silently re-enable them. No motion slots, gains or calibration were changed, and API v2 is not uploaded. Full evidence: [trial report](movements/trials/REPORT.md).

Updated source advertises `apiVersion:2` and adds **Save current pose as idle** while disarmed. This captures current commanded angles, checks they have not changed since the UI read, and persists a profile without moving motors. Default test radius is ±3° (RF 28–34°), speed 5°/s, correction limit 1°, tilt cutoff 10°, and zero PID gains. Radius is selectable 1–10°. Limits describe the planned test envelope, not physically measured joint clearance.

**Return to saved idle** requires an armed session and plays a smooth 2-second return. Boot and legacy Stand use the saved neutral when its stored GPIO map and trims still match. A profile from another mapping is ignored on boot; changing the pin map clears the active profile. Preserve the mapping and recapture after changing assembly/trim. Before the first profile has been saved, boot still uses the original centre pulses, so support the mechanics during that first upload/startup. The dashboard's electrical servo calibration/assembly buttons still deliberately send 90° and are not an idle command.

Custom-idle playback requires a fresh calibrated IMU even with PID off. The updated sample-validity guard also requires acceleration magnitude 0.75–1.25g for slow posture control; `imuSampleValid` and `accelMagnitude` expose this. It rejects the recorded magnitude 521 sample but does not repair the underlying sensor/data fault. Missing/stale data (>250 ms), lost calibration or tilt beyond the configured cutoff stops the session. PID stays off unless explicitly enabled. Legacy `/move` gaits assume 90° and return 409 while a custom profile is active; automatic lift kicking is also suppressed for that profile. Use Studio sequences for this assembly. These guards do not detect a table edge or guarantee a stable gait.

Six candidate JSON files for the latest observed pose are in [movements/tabletop_80659045](movements/tabletop_80659045/manifest.json), with the earlier pose preserved in [movements/tabletop_rf31](movements/tabletop_rf31/manifest.json): Idle hold, Attentive, Little nod, Curious, Happy wiggle and Sleepy rock. Every motion returns to its library’s starting pose and offsets joints by at most 1°. They are **locally saved, physically untested and not yet installed on Bob**. The manifest keeps that validation status explicit for future BOB_ROBO_CONVO work; integration remains deferred. Regenerate with:

    python3 BOB_V2/tools/prepare_tabletop_library.py --neutral 80 65 90 45 --output BOB_V2/movements/tabletop_80659045

After uploading API v2 and restoring the correct standing pose, capture idle, calibrate, arm, import a file, test once while observing, and save it into an available slot. Export existing slot contents before replacing them. Never classify an accepted PWM command as a physically verified gesture.

## First wired test

Until a custom idle profile is saved, firmware sends its existing centre pulses during startup. Support the robot and check servo power, ground and mechanical clearance before powering it.

1. Check /hw, /logs and /imu first. The MPU should answer and pitch/roll should follow a gentle tilt. Check screen, camera, microphone and speaker through the existing dashboard, one at a time.
2. Use the existing servo trim controls to align the assembled joints. Confirm which joint has the SG92R. Set travel limits separately for each joint; the default logical range 60–120° is a starting range, not a measured mechanical guarantee.
3. Verify the current assembled standing pose (latest observed 80/65/90/45), select a small test radius, and **Save current pose as idle**. Then **Calibrate IMU** while Bob is still. Do not centre this assembly to 90/90/90/90.
4. Click **Arm controls**. This acquires control and holds the current command. It does not start a sequence or enable PID.
5. Leave PID off. Preview small pose changes, one joint at a time, to verify direction, range and clearance. Add successful poses as keyframes and save a short sequence.
6. Confirm the pitch/roll correction signs on the supported robot before testing **Enable PID**. Start with I=0 and a small P; adjust D only after observing the response. Stop if the correction makes tilt worse, correct the mapping, then recalibrate.
7. Compare telemetry and actual physical behavior before storing a tuned setup. An onboard camera may not see Bob's legs or foot contact, so a person observing the robot is still needed.

The factory defaults (P=.35, I=0, D=.03, maximum correction 8°, speed 40°/s; custom idle capture replaces these with zero gains and the limits above) are conservative software starting values, **not gains tuned or verified on your assembled robot**.

## PID scope and stop behavior

This is an outer body-tilt controller. The hobby servos already implement internal position control. Bob's MPU supplies 20 Hz accelerometer-derived pitch/roll, with filtered measurements and derivative-on-measurement. Joint commands update at up to 50 Hz. Each joint receives:

    base angle + pitch mix × pitch PID output + roll mix × roll PID output

The code clamps controller output and joint travel, limits command speed, and suppresses integral accumulation while saturated. These follow standard [PID anti-windup principles](https://www.mathworks.com/help/simulink/slref/anti-windup-control-using-a-pid-controller.html). Accelerometer tilt is affected by translation/impacts; this initial controller is intended for slow posture experiments, not a proven dynamic walking stabilizer. It does not learn gaits autonomously.

PID requires a fresh calibration and a recent sample. With PID active or a custom-idle sequence playing, loss of IMU/calibration, a sample older than 250 ms, or tilt beyond the configured cutoff stops the session. A control-loop gap over 250 ms also stops an armed session when the loop resumes. No heartbeat for 3 seconds expires control ownership. Stop holds the last PWM command; it does **not** remove servo power. It is a software stop, not an independent hardware emergency switch.

**Stop & hold**, leaving/hiding the editor, a fault or heartbeat expiry ends the session and requires fresh IMU calibration before PID/lift reactions resume. PID is always off after a restart or a new arm. The control token is local ownership arbitration, not Internet authentication. Keep this HTTP service on the trusted local robot network.

While armed, Motion Studio owns the servos. Existing blocking movements, trim changes, audio operations and calibration return HTTP 409. Read-only IMU/state/diagnostic polling remains available; the dashboard's Stop/Stand button stops and holds the Studio session. Original blocking gait routines are available only without a custom idle profile; their stop latency differs from the new nonblocking sequences.

Move times may extend when the speed limit requires more time. A keyframe's hold time begins after the commanded output reaches its target (within one command degree), not after a measured servo arrival.

## Live access for later assisted tuning

When the circuit is ready, first run a read-only report from the Mac:

    python3 BOB_V2/tools/motion_client.py --host 192.168.1.9 inspect --output /tmp/bob-hardware.json

Use Bob's current displayed IP. This reads hardware/IMU/motion status; it does not arm or exercise hardware. It cannot prove speaker output, microphone quality, motor motion or screen appearance. Those need their individual dashboard tests and observation.

After confirming the mechanism and calibration, a short explicit trial can play an exported file and record state:

    python3 BOB_V2/tools/motion_client.py --host 192.168.1.9 play bob-movement.json --seconds 20 --telemetry /tmp/bob-trial.jsonl

Add --pid only after validating correction direction and calibrating. The client sends a heartbeat, stops on completion/deadline/error, and holds the last command. There is no automatic upload or hardware exercise in the development checks.

The MotionClient context manager also supports live parameter changes for future tuning:

    from motion_client import MotionClient

    with MotionClient("192.168.1.9") as robot:
        status = robot.state()
        config = status["config"]
        config["gains"][0] = [0.25, 0, 0.03]
        robot.command("config", config=config, persist=False)
        # Issue bounded play/PID commands only when the robot is ready.
        # Exiting the context stops this owned session, even after an exception.

## HTTP contract

All /motion/* endpoints return JSON and wait for hardware initialization. Mutating calls use POST with Content-Type: application/json. The disarmed-only `/motion/setup` capture endpoint is token-free and never writes PWM. Other configuration/play/storage actions require ownership. Obtain token from /motion/arm and send it in every heartbeat/configuration/play/storage request. A new token is needed after any stop or timeout. JSON bodies are limited to 8192 bytes.

| Endpoint | Request |
|---|---|
| GET /motion/state | Telemetry, settings, commanded angles, ownership/playing/PID flags; no token exposed |
| GET /motion/library | Six slots with saved sequence documents or null |
| POST /motion/setup | {"expected":[80,65,90,45],"radius":3}; persists custom idle, small limits and zero gains while disarmed; no movement |
| POST /motion/arm | {} → token and leaseMs:3000; rejects another owner or lift reaction |
| POST /motion/heartbeat | {"token":"..."} every 700–1000 ms; expired tokens never re-arm |
| POST /motion/stop | {} for operator stop; {"token":"..."} for owner-scoped release |
| POST /motion/config | {"token":"...","config":{...},"persist":false} |
| POST /motion/pid | {"token":"...","enabled":true,"target":[0,0]}; target within ±15° |
| POST /motion/play | {"token":"...","sequence":{...},"repeats":1} |
| POST /motion/save | {"token":"...","slot":0,"sequence":{...}}; slots 0–5 |
| POST /motion/delete | {"token":"...","slot":0} |

Configuration schema (also returned: `neutral`, `customNeutral`, `servoPins`, `servoTrims`; use the full returned config when editing):

    {
      "gains": [[0.35, 0, 0.03], [0.35, 0, 0.03]],
      "joints": [[60, 120, 1, 0], [60, 120, 0, 1], [60, 120, 1, 0], [60, 120, 0, -1]],
      "correction": 8,
      "slew": 40,
      "cutoff": 35
    }

Gains are pitch/roll arrays [P,I,D]; joints are four arrays [min,max,pitchMix,rollMix]. P is bounded 0–5, I/D 0–2, mixes −1…1, correction 1–15°, slew 5–120°/s, cutoff 10–60°. Invalid/NaN values, limits incompatible with trims/current commands/playing sequence, and out-of-range poses are rejected. Saving settings does not persist PID-on or ownership state.

Sequence schema (top-level version is used for storage/export):

    {
      "version": 1,
      "name": "Small sway",
      "frames": [
        {"angles": [93, 92, 87, 88], "moveMs": 1000, "holdMs": 250},
        {"angles": [90, 90, 90, 90], "moveMs": 1000, "holdMs": 500}
      ]
    }

A sequence also carries an optional `"mode"`: `"joints"` (default — the frames above are absolute joint angles) or `"tilt"` (posture control). In tilt mode each frame is a target body orientation instead of joint angles:

    {
      "version": 1, "name": "Nod", "mode": "tilt", "expression": 0,
      "frames": [
        {"tilt": [6, 0], "moveMs": 650, "holdMs": 250},
        {"tilt": [0, 0], "moveMs": 650, "holdMs": 400}
      ]
    }

`tilt` is `[pitch,roll]` in degrees, each within ±15° and the configured tilt cutoff. Tilt playback holds the pose at `neutral` and moves the four joints only through PID correction, so **PID must be enabled first** (`/motion/play` returns 409 otherwise). Orientation convergence depends on the tuned loop and servo authority, so posture frames advance on time once their ramp completes. `/motion/state` reports the active `seqMode`. Generate a starter library (joint + tilt) with `BOB_V2/tools/prepare_expression_library.py`; see `BOB_V2/movements/expression_library/`. Older firmware without tilt support ignores unknown modes only if the frames still parse — upload the updated build to use posture sequences.

Names are 1–24 UTF-8 bytes. Settings and movements use the separate bobmotion Preferences namespace; existing trims/audio/WiFi preferences remain separate. Persistence checks bytes written and reports failures. See [Espressif Preferences documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html).

## Development verification

    clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
      BOB_V2/tests/motion_engine_test.cpp -o /tmp/bob-motion-engine-test
    /tmp/bob-motion-engine-test
    python3 -m unittest discover -s BOB_V2/tests -p 'test_*.py' -v

These validate controller logic/client lifecycle without driving a robot. Firmware compilation and JavaScript syntax checks are separate from physical tuning and browser visual validation.
