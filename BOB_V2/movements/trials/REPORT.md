# Tabletop trial evidence — 2026-09-27

Robot: 192.168.1.9. Saved GPIO roles LH/LF/RH/RF=38/40/39/41, trims zero. Running firmware lacks Motion API v2. Observed rest command [80,65,90,45]; earlier user-described rest [90,90,90,31] must not be assumed current.

## What was tested

- `rf_plus1_2026-09-27.json`: sandbox permission denied before any robot request.
- `rf_plus1_live_2026-09-27.json`: baseline reads only; aborted on −58.44° pitch / −179.25° roll. No motor command.
- `imu_stationary_2026-09-27.json`: 30 read-only status/raw-IMU pairs. Commanded pose stayed [80,65,90,45]. One raw sample ax−521/ay0/az0/gx0/gy0/gz0 had magnitude521 versus calibrated rest980; normal magnitude ~1022. This suggests invalid data or unusual acceleration; wiring/sensor cause is not established.
- `rf_plus1_retry_2026-09-27.json`: six healthy baseline samples; RF45→46 command acknowledged and echoed in eight status samples; RF46→45 acknowledged and echoed in the return sample. That return sample contained the same extreme IMU spike, so the final settling check failed. A subsequent read confirmed [80,65,90,45], disarmed, PID off, ordinary tilt again.

**The user reported no visible movement. PWM command acknowledgement is not motor-position feedback. No physical servo movement, table displacement, successful tuning or stable gesture has been verified.** One degree was intentionally small and not intended to walk Bob across the table.

The trial JSON `completed:false` and `returned:false` flags describe the failed final verification, not absence of the return command: the `phase:returned` sample confirms the command value45. The runner now additionally records command acknowledgement flags for future traces; old evidence is retained unchanged.

## Persistent change on Bob

`/liftdetect?on=0` succeeded and saves automatic lift kicking OFF. This avoids sensor spikes triggering the old90° routine. No gains, servo trim/map, motion library slots or calibration were written. No firmware uploaded. No legacy walking/dance command sent.

## Reusable work and next test

Generated libraries are local JSON only. Current-pose candidates live in `../tabletop_80659045`; earlier-pose candidates in `../tabletop_rf31`. They remain marked physically untested. New source includes custom-idle capture, narrow travel limits, zero-gain initial setup and IMU validity guards. These do not repair sensor hardware or prove stable motion.

Next: inspect IMU wiring/power/data quality with Bob still, flash the prepared firmware using the documented strap-pin precautions, verify `apiVersion:2`, adopt the correct assembled idle, and run an observed small trial. Confirm actual motion before considering PID direction or gains. Keep BOB_ROBO_CONVO automatic use deferred.
