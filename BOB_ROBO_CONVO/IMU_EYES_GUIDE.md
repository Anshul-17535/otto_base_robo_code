# IMU visualization and scripted eyes

Updated 2026-10-03. Firmware source is in `../BOB_V2/Otto_ESP32S3`.

## Robot dashboard (requires reflash)

After uploading the updated sketch, hard-refresh `http://192.168.1.8` and open
**IMU**. The dashboard's gzip assets normally have a one-day browser cache.

- **Balanced** is now the default (0.20 s smoothing parameter), with Cinematic
  (0.32 s) and Responsive (0.10 s). A critically damped spring preserves velocity
  between sensor packets. Median-of-three rejects isolated spikes; a smaller
  0.12° deadband reduces jitter without the earlier sticky movement. Angle
  unwrapping avoids full rotations across ±180°. Displayed angles follow the
  rendered pose; raw acceleration and gyro diagnostics remain below.
- **Reverse pitch / Reverse roll** independently invert the visual axes and
  persist in this browser. Start with **Reverse roll: ON** for the reported
  clockwise/anticlockwise mismatch. These never change sensor values or motors.
- **FRONT** is cyan with eyes; **BACK** is amber with vents. View back changes
  the camera, not the robot. Recording view enlarges the cube; exit with its
  button or Escape. Only pitch and roll are shown, not measured yaw. Physical
  front alignment depends on the existing IMU mounting and must be checked.
- Raw readouts, sensor processing, calibration, pins and motor control are
  unchanged. Display smoothing does not fix physical IMU faults.

**Play liquid eyes** starts an 11-second scripted TFT animation using the same
normal black circular eyes, white highlights and mouth drawing helpers as the
regular face. Eyes slide left (2.4 s), right (2.8 s), recenter (1.2 s), shake
(1.6 s), then smoothly scowl. The mouth follows with a slight delay, banks with
the face, opens into an O during the shake and ends with gritted teeth. Only the
final **HEY! Easy on me!** line appears in the bottom bar; no directional or
intermediate captions. Quintic easing gives gentle starts/stops, and the shake
ramps in/out. This is simulated, not live IMU tracking. No speech or motors.
**Stop eyes demo** restores the face early. The normal face also returns at the
end. Legacy display/audio/movement commands return 409 during the demo to avoid
interference; read-only IMU polling continues. Existing autonomous lift behavior
is not reconfigured by this button.

Implementation: `/eyes/demo` and `/eyes/demo?stop=1`, a 33 ms minimum frame
interval, elapsed-time animation and a 160×100 RGB565 offscreen canvas (32 KB
heap allocated on first play). Canvas frames avoid erase/redraw flicker; actual
frame rate depends on SPI time and other firmware tasks. A blocking operation
elsewhere can still interrupt animation. Allocation failure restores the face.

## Optional Python recording view (no reflash)

Restart `server.py`, then choose **Motion studio** from the conversation UI or
open `http://localhost:8090/imu-view`. It provides a larger version of the cube,
clean view, labeled sides and smoothing options. Escape exits clean view.
`/api/imu` proxies only GET `/imu`; it yields while speech/game or idle work owns
the robot. Offline or busy status holds the last pose without fabricated motion.
This page does not replace the robot-hosted dashboard.

## Validation and next step

- Firmware compile passed for ESP32-S3, 16 MB flash / OPI PSRAM / huge_app.
- Firmware cube tests cover spike suppression, deadband, invalid inputs,
  wraparound, independent direction reversal and equal results at 60/120 fps. Run from BOB_ROBO_CONVO:
  `node tests/check_imu_smoothing.cjs`.
- Extracted dashboard JavaScript passes `node --check`.
- Existing 15 Python tests pass; host smoothing also checked offline.
- Browser skill failed to connect; no screenshot/visual claim. Latest firmware revision not
  uploaded; TFT animation and live sensor motion not physically verified.

Next: reflash when hardware/upload wiring is ready, hard-refresh the dashboard,
then visually check cube orientation and the screen-only demo. Preserve the
existing pin map and follow `../BOB_V2/WIRING.md` upload precautions.
