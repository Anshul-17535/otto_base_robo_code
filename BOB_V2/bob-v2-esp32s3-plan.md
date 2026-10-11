> **Historical plan:** the pin proposals below are not the current wiring. Use [WIRING.md](WIRING.md) for the active mixed-servo + screen + camera setup. Do not wire GPIO33–37 to peripherals on the N16R8.

# BOB V2 — ESP32-S3 Migration Plan

## Top-Level Overview

**Goal:** Migrate the existing Otto robot firmware from the classic ESP32 to the **ESP32-S3**,
integrate the S3's **built-in camera** directly into the main firmware (replacing the separate
`Otto_ESP32_CAM` sketch), and update the Web UI to include a camera view panel.
**SAM TTS is removed** from the firmware — audio playback is handled entirely by the
`BOB_ROBO_CONVO` server on the Mac (Mac `say` → ffmpeg → POST `/mactts`), same as today.
A new `BOB_V2/` directory holds all migrated code. The original `Otto_ESP32/` folder is
left untouched as a reference.

### What Changes
| Area | Classic ESP32 | ESP32-S3 (BOB V2) |
|---|---|---|
| Arduino board target | `esp32:esp32:esp32` | `esp32:esp32:esp32s3` |
| I2S API | Legacy `driver/i2s.h` (port 0 / port 1) | Same legacy API — S3-incompatible register fix block removed |
| SPH0645 timing fix | `I2S_TIMING_REG` + `I2S_CONF_REG` bits (chip-specific) | Removed — registers don't exist on S3 |
| Camera | Separate `Otto_ESP32_CAM` sketch on a second board | Built-in to main firmware via `esp_camera.h` |
| SAM TTS | `ESP8266SAM` + `AudioOutput.h` shim — on-device robot voice | **Removed** — TTS handled by `BOB_ROBO_CONVO` Mac server (no change to server) |
| `AudioOutput.h` shim | Required by SAM | **Removed** (no longer needed) |
| `USE_SAM_TTS` flag + `speakVoice()` | On-device voice synthesis | **Removed** |
| GPIO numbering | Classic ESP32 GPIO map | ESP32-S3 GPIO map — several conflicts resolved |
| SPI / LEDC | VSPI + LEDC | FSPI default on S3 + LEDC (same API, different default bus) |
| SSH / remote shell | **Not available** on ESP32/S3 — see SSH note below | Same: no SSH possible |

### What Stays the Same
- All servo gait, dance, and motion code (zero changes needed)
- All TFT face animation code (driver + GFX calls are identical)
- All web UI HTML/JS logic (minor endpoint addition for camera)
- All NVS preferences, log ring, BNO055 IMU code
- `/mactts` POST endpoint — the Mac server still streams WAV to the robot speaker
- `speakText()` buzzer-beep fallback — kept as fallback if Mac server is offline
- WiFi AP+STA dual-mode, mDNS, WebServer
- `BOB_ROBO_CONVO/` — **no changes at all** to the Mac server or its lib modules

---

## SSH on ESP32-S3 — Short Answer

**No, you cannot SSH into an ESP32-S3.**

SSH requires a full TCP/IP stack with an SSH daemon (OpenSSH or Dropbear), a filesystem for
host keys, and enough RAM/flash for the crypto primitives. The ESP32-S3 runs a cooperative
RTOS with ~512 KB SRAM and the Arduino/ESP-IDF stack. There is no SSH server library that
works on it.

**What you can do instead:**
- Serial monitor over USB (fastest for debugging)
- The existing `/logs` web endpoint (survives crashes — RTC ring buffer)
- The existing WebServer routes (`/diag`, `/micscan`, `/miclevel`) reachable over WiFi
- Telnet (plain TCP, no encryption) — `ESPTelnet` library is an option if you need a
  live shell-like interface over the network, but it is not SSH

---

## New Directory Layout

```
BOB_V2/
  Otto_ESP32S3/
    Otto_ESP32S3.ino      ← main sketch (migrated + SAM removed)
    html_page.h           ← updated: adds camera panel to Web UI
    cam_pins_s3.h         ← NEW: ESP32-S3 camera pin definitions
  README.md               ← description of BOB V2 changes
```

Note: `AudioOutput.h` is **not copied** — it only existed to support SAM TTS, which is removed.

---

## Sub-Tasks

---

### Sub-Task 1 — Create `BOB_V2/` folder structure and copy base files

**Intent**
Set up the `BOB_V2/Otto_ESP32S3/` directory with copies of the source files so every
subsequent sub-task edits the V2 copy without touching the original.

**Expected Outcomes**
- `BOB_V2/Otto_ESP32S3/Otto_ESP32S3.ino` — copy of `Otto_ESP32/Otto_ESP32.ino` (SAM removal happens in Sub-Task 2)
- `BOB_V2/Otto_ESP32S3/html_page.h` — copy (to be edited in Sub-Task 5)
- `BOB_V2/Otto_ESP32S3/cam_pins_s3.h` — new empty header (filled in Sub-Task 3)
- `AudioOutput.h` is **NOT copied** — it only served SAM TTS

**Todo List**
1. Copy `Otto_ESP32/Otto_ESP32.ino` → `BOB_V2/Otto_ESP32S3/Otto_ESP32S3.ino`
2. Copy `Otto_ESP32/html_page.h` → `BOB_V2/Otto_ESP32S3/html_page.h`
3. Create empty `BOB_V2/Otto_ESP32S3/cam_pins_s3.h`

**Relevant Context**
- Source: `Otto_ESP32/` directory (3 files — `AudioOutput.h` excluded)
- `AudioOutput.h` is a SAM TTS shim — dropped in Sub-Task 2

**Status** `[ ] pending`

---

### Sub-Task 2 — Remove SAM TTS and update library includes / board conditionals

**Intent**
Two things happen in this sub-task:

1. **Remove SAM TTS entirely.** The original sketch has `#define USE_SAM_TTS 1`,
   includes `AudioOutput.h` + `<ESP8266SAM.h>`, defines `AudioOutputLegacySpeaker`,
   and has the `speakVoice()` function. All of this is deleted. The Web UI's
   "Speak" button already posts to `/mactts` (the Mac server pipeline) — the buzzer
   `speakText()` path is kept as a silent fallback. No changes to `BOB_ROBO_CONVO/`.

2. **Remove the S3-incompatible register fix block.** The `soc/i2s_reg.h` include and the
   SPH0645 timing-fix `REG_SET_BIT` calls only compile on the classic ESP32. They must be
   removed or they will fail to compile on S3.

**Expected Outcomes**
- `#define USE_SAM_TTS` removed (or set to 0 permanently — prefer full deletion)
- `#include "AudioOutput.h"` and `#include <ESP8266SAM.h>` removed
- `class AudioOutputLegacySpeaker` block removed
- `speakVoice()` function removed
- Any Web UI handler that calls `speakVoice()` replaced with `speakText()` (buzzer)
  or removed if the `/mactts` endpoint already handles it (check `handleSpeak()`)
- `#include "soc/i2s_reg.h"` and its `#if CONFIG_IDF_TARGET_ESP32` guard removed
- SPH0645 timing-fix `REG_SET_BIT` block inside `i2s_mic_init()` removed
- Board comment header updated: "ESP32" → "ESP32-S3"
- Libraries section in header comment updated: remove ESP8266SAM / ESP8266Audio entries

**Todo List**
1. Delete `#define USE_SAM_TTS 1` and the `#if USE_SAM_TTS … #endif` block that includes
   `AudioOutput.h` and `ESP8266SAM.h` (original lines 94–107)
2. Delete `class AudioOutputLegacySpeaker` (original lines 1003–1024)
3. Delete `speakVoice()` function (original lines 1029–1045)
4. In `handleSpeak()` (line 3082): delete the `#if !USE_SAM_TTS … #endif` block
   (lines 3095–3097) and the `#if USE_SAM_TTS … #endif` block (lines 3103–3112).
   The function then simply always calls `speakText(text)` — the buzzer fallback.
   The `wantVoice` variable becomes unused; remove it too.
5. Remove `#if CONFIG_IDF_TARGET_ESP32` / `#include "soc/i2s_reg.h"` block (lines 76–78)
6. Remove SPH0645 timing-fix `REG_SET_BIT` block inside `i2s_mic_init()` (lines 716–724)
7. Update top-of-file comment block: board name → "ESP32-S3", drop SAM library entries

**Relevant Context**
- `USE_SAM_TTS` flag: original line 94; the `#if` block wraps lines 95–107
- `AudioOutputLegacySpeaker` class: original lines 1003–1024
- `speakVoice()`: original lines 1029–1045
- `handleSpeak()` / the `/speak` route: search for `speakVoice(` in the file to find callers
- S3 register fix: original lines 76–78 (include) and 716–724 (i2s_mic_init body)
- `BOB_ROBO_CONVO/lib/robo_tts.py` — this is unchanged; it already uses Mac `say` →
  ffmpeg → POST `/mactts`, which the S3 firmware still serves via `handleMacTTS()`

**Status** `[ ] pending`

---

### Sub-Task 3 — Remap GPIO pins for ESP32-S3-N16R8 + CSI camera

**Intent**
The board is an **ESP32-S3-N16R8** (16 MB flash, 8 MB OPI PSRAM) with an OV2640 camera
wired to its CSI/DVP connector. The classic ESP32 GPIO map is entirely different —
several pins used in the current sketch do not exist on the S3 (GPIO32–36 are gone),
others are strapping pins. Camera pins are fixed by the CSI connector layout and
cannot be changed. All other peripherals must be mapped to the free GPIOs that remain.

**ESP32-S3-N16R8 CSI Camera Pin Map (fixed — wired by the module)**
| Camera Signal | ESP32-S3 GPIO |
|---|---|
| PWDN | -1 (not wired) |
| RESET | -1 (not wired) |
| XCLK | 15 |
| SIOD (SDA) | 4 |
| SIOC (SCL) | 5 |
| Y9 (D7) | 16 |
| Y8 (D6) | 17 |
| Y7 (D5) | 18 |
| Y6 (D4) | 12 |
| Y5 (D3) | 10 |
| Y4 (D2) | 8 |
| Y3 (D1) | 9 |
| Y2 (D0) | 11 |
| VSYNC | 6 |
| HREF | 7 |
| PCLK | 13 |

**Free GPIOs after reserving camera + USB (GPIO19/20) + strapping (GPIO0, GPIO3, GPIO45, GPIO46):**
GPIO1, GPIO2, GPIO14, GPIO21, GPIO33, GPIO34, GPIO35, GPIO36, GPIO37, GPIO38, GPIO39, GPIO40, GPIO41, GPIO42, GPIO43 (UART TX — avoid), GPIO44 (UART RX — avoid), GPIO47, GPIO48

**Peripheral → GPIO remap**
| Signal | Classic ESP32 GPIO | ESP32-S3 GPIO | Notes |
|---|---|---|---|
| TFT CS | 5 | 40 | |
| TFT RST | 32 | 38 | GPIO32 doesn't exist on S3 |
| TFT DC | 33 | 39 | GPIO33 doesn't exist on S3 |
| TFT MOSI | 23 | 35 | SPI data |
| TFT CLK | 18 | 36 | SPI clock (GPIO18 taken by camera Y7) |
| I2S SPK BCLK | 25 | 1 | |
| I2S SPK LRC | 26 | 2 | |
| I2S SPK DIN | 27 | 14 | |
| I2S MIC SCK | 32 | 33 | |
| I2S MIC WS | 33 | 34 | |
| I2S MIC SD | 14 | 21 | GPIO14 taken by SPK DIN — use 21 |
| Servo LH | 12 | 41 | GPIO12 taken by camera Y6 |
| Servo LF | 13 | 42 | GPIO13 taken by camera PCLK |
| Servo RH | 15 | 47 | GPIO15 taken by camera XCLK |
| Servo RF | 19 | 48 | GPIO19 is USB D- on S3 |
| I2C SDA (BNO055) | 21 | 37 | GPIO21 used for mic SD above |
| I2C SCL (BNO055) | 22 | 38 | Wait — GPIO38 used for TFT RST; use GPIO... see note |

**Conflict resolution note:**
GPIO38 is used for both TFT RST and I2C SCL above. Resolve by:
- TFT RST → GPIO48 (move Servo RF to GPIO47, TFT RST to GPIO48)
- Servo RF → GPIO47, Servo RH → GPIO48... cascade as needed

**Final clean assignment (no conflicts):**
| Signal | GPIO |
|---|---|
| TFT CS | 40 |
| TFT RST | 41 |
| TFT DC | 42 |
| TFT MOSI | 35 |
| TFT CLK | 36 |
| I2S SPK BCLK | 1 |
| I2S SPK LRC | 2 |
| I2S SPK DIN | 14 |
| I2S MIC SCK | 33 |
| I2S MIC WS | 34 |
| I2S MIC SD | 21 |
| Servo LH | 37 |
| Servo LF | 38 |
| Servo RH | 39 |
| Servo RF | 48 |
| I2C SDA | 47 |
| I2C SCL | 45 |

**Note:** GPIO45 and GPIO46 are strapping pins — safe to use as normal GPIO after boot,
but tie them to known levels (3.3V or GND via resistor) to avoid unexpected boot modes.
If there are concerns about GPIO45, swap I2C SCL to GPIO46 instead.

**Todo List**
1. Create `cam_pins_s3.h` with the ESP32-S3-N16R8 CSI camera pin `#define` block above
2. In `Otto_ESP32S3.ino`, update `TFT_CS`, `TFT_RST`, `TFT_DC`, `TFT_MOSI`, `TFT_CLK`
3. Update `I2S_SPK_BCLK`, `I2S_SPK_LRC`, `I2S_SPK_DIN`
4. Update `I2S_MIC_SCK`, `I2S_MIC_WS`, `I2S_MIC_SD`
5. Update `SERVO_PIN_LH`, `SERVO_PIN_LF`, `SERVO_PIN_RH`, `SERVO_PIN_RF`
6. Update I2C SDA/SCL in the BNO055 `Wire.begin()` call and comments
7. Add `#include "cam_pins_s3.h"` to includes section

**Relevant Context**
- Original pin defines: lines 245–249 (TFT), 624–632 (I2S), 1129–1132 (Servo)
- Camera pins 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18 are **all reserved** by the CSI connector
- ESP32-S3 strapping pins (boot-sensitive): GPIO0, GPIO3, GPIO45, GPIO46
- ESP32-S3 USB pins: GPIO19 (D-), GPIO20 (D+) — do not use for peripherals
- UART0: GPIO43 (TX), GPIO44 (RX) — avoid unless you disable serial monitor

**Status** `[ ] pending`

---

### Sub-Task 4 — Add camera initialisation and streaming endpoints

**Intent**
The ESP32-S3 has a built-in camera interface (DVP/CSI). Add camera init in `setup()` and
two HTTP endpoints to the main sketch: `GET /cam/stream` (MJPEG) and `GET /cam/snapshot`
(single JPEG). This makes the separate `Otto_ESP32_CAM` sketch unnecessary — one firmware,
one device. Camera is initialised after WiFi and TFT are ready; failure is logged but does
not prevent the rest of the firmware from running.

**Expected Outcomes**
- `cam_pins_s3.h` `#include`d at top of `Otto_ESP32S3.ino`
- `#include "esp_camera.h"` added
- `camReady` boolean flag — `false` if camera init fails
- `cameraInit()` function: sets up `camera_config_t` using `cam_pins_s3.h` pins,
  frame size `FRAMESIZE_QVGA` (320×240) for low latency, JPEG quality 12
- `handleCamStream()` — MJPEG boundary streaming (same pattern as `Otto_ESP32_CAM.ino`)
- `handleCamSnapshot()` — single JPEG capture + send
- Both endpoints registered in `setup()` alongside existing routes
- If `camReady == false`, both endpoints return `503 Service Unavailable`

**Todo List**
1. Add `#include "esp_camera.h"` and `#include "cam_pins_s3.h"` to includes section
2. Declare `bool camReady = false;` near other global state flags
3. Implement `cameraInit()` — camera_config_t, esp_camera_init(), log result
4. Implement `handleCamStream()` — multipart/x-mixed-replace MJPEG loop
5. Implement `handleCamSnapshot()` — single-shot JPEG response
6. In `setup()`, call `cameraInit()` after WiFi init; register `/cam/stream` and
   `/cam/snapshot` routes on `server`

**Relevant Context**
- `Otto_ESP32_CAM/Otto_ESP32_CAM.ino` — reference MJPEG streaming implementation
- Camera config pattern: `CAMERA_MODEL_XIAO_ESP32S3` or `CAMERA_MODEL_ESP32S3_EYE`
  depending on module — `cam_pins_s3.h` abstracts this
- `esp_camera.h` is provided by the ESP32 Arduino core — no extra library install

**Status** `[ ] pending`

---

### Sub-Task 5 — Add Camera panel to Web UI (`html_page.h`)

**Intent**
Add a collapsible "Camera" card to the existing Web UI in `html_page.h`. The panel shows
a live MJPEG stream when enabled, a snapshot button, and an on/off toggle. Styled to match
the existing dark-theme cards. The camera section is hidden by default (to avoid loading the
stream on open) and toggled by a button. If the camera endpoint returns 503, a short error
message is shown instead of the stream.

**Expected Outcomes**
- New `.card` section titled "Camera" inserted after the Audio card in the HTML
- CSS added for `#camBox`, `#camImg`, stream on/off button (matching `.rec-btn` style)
- JavaScript: `startCam()` sets `#camImg.src = '/cam/stream'`, `stopCam()` clears it
- Snapshot button (`GET /cam/snapshot`) opens the JPEG in a new tab
- If stream fails (503 — no camera fitted), shows a `<span>` "Camera not available on
  this firmware build" in the card body

**Todo List**
1. In `html_page.h`, add camera card CSS (`.cam-btn`, `#camBox`, `#camImg`)
2. Add the HTML card block with stream box, Start/Stop button, Snapshot button
3. Add JavaScript `startCam()`, `stopCam()`, snapshot handler
4. Test that existing card layout is not broken (max-width, gap, card order)

**Relevant Context**
- `html_page.h` existing card pattern: lines 20–80 (CSS), rest of file (HTML + JS)
- `Otto_ESP32_CAM/cam_html.h` — reference MJPEG stream UI pattern

**Status** `[ ] pending`

---

### Sub-Task 6 — Update top-level comments, wiring doc, and write description MD

**Intent**
Update the sketch's header comment block to reflect the S3 hardware and new pin map.
Update `WIRING.md` with an S3-specific section. Write `BOB_V2/README.md` — the description
document for the BOB V2 plan — covering what changed, why, and how to flash.

**Expected Outcomes**
- `Otto_ESP32S3.ino` header comment: board = "ESP32-S3", updated pin table, updated
  libraries section (note `esp_camera.h` built-in)
- `BOB_V2/README.md` written with:
  - What BOB V2 is (ESP32-S3 upgrade)
  - What changed vs V1 (library guards, pin map, camera integration)
  - SSH answer (no — and what to use instead)
  - How to flash (Arduino IDE board: `esp32:esp32:esp32s3`, partition: `Huge APP`)
  - Camera module notes (XIAO S3 Sense vs generic S3 devkit)
  - Known differences from V1

**Todo List**
1. Update the header comment block at the top of `Otto_ESP32S3.ino`
2. Write `BOB_V2/README.md` with all sections listed above
3. Add an "ESP32-S3 Wiring" section to `WIRING.md` (or note to refer to README.md)

**Relevant Context**
- Original header: lines 1–55 of `Otto_ESP32.ino`
- `WIRING.md` existing sections 1–13 — add section 14 for S3 differences

**Status** `[ ] pending`

---

## Key Decisions / Trade-offs

| Decision | Chosen Approach | Rationale |
|---|---|---|
| SAM TTS | Removed from firmware | `BOB_ROBO_CONVO` Mac server already handles all TTS via `/mactts`; on-device SAM was redundant and added two extra library dependencies |
| I2S API version | Keep legacy `driver/i2s.h` | New `i2s_std.h` API requires full rewrite of all audio calls — no functional gain; legacy still compiles on S3 with core ≥ 3.x |
| Camera location | Merged into main firmware | Eliminates second board, second flash, second WiFi config |
| Camera default resolution | QVGA 320×240 | Minimises heap pressure on S3 while audio + servos + WiFi are all running |
| Pin remap strategy | Map to non-strapping, non-camera GPIOs | S3 has fewer freely-usable GPIOs than classic ESP32; systematic audit prevents boot failures |
| `Otto_ESP32/` folder | Left unchanged | Reference / rollback — user can always reflash the original |
| SSH | Not implemented | Not possible on ESP32-S3; documented in README |

## Files Created / Modified

| File | Action |
|---|---|
| `BOB_V2/README.md` | CREATE — description of the plan and V2 changes |
| `BOB_V2/Otto_ESP32S3/Otto_ESP32S3.ino` | CREATE — migrated + SAM removed |
| `BOB_V2/Otto_ESP32S3/html_page.h` | CREATE — updated with camera panel |
| `BOB_V2/Otto_ESP32S3/cam_pins_s3.h` | CREATE — S3 camera pin definitions |
| `WIRING.md` | MODIFY — add S3 wiring section |
| `Otto_ESP32/` | UNCHANGED — kept as reference |
| `Otto_ESP32_CAM/` | UNCHANGED — kept as reference |
| `BOB_ROBO_CONVO/` | UNCHANGED — Mac server works as-is |
