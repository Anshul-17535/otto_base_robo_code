# Archived full-system wiring — do not wire from this document

This is the previous, unvalidated full-system map. It contains known memory-pin conflicts and obsolete bring-up instructions. It does not describe the active servo-only firmware. Use [the current wiring guide](../WIRING.md). Future peripherals need a new pin plan; GPIO40/41 are now servo signals.

> **Pin-map audit (September 2026): the tables below describe the existing wiring, not a validated N16R8 design.** GPIO33–37 are reserved for octal memory on ESP32-S3 R8 variants. This sketch assigns microphone clocks to 33/34, TFT to 35/36, and left hip to 37. Disabling PSRAM does not make those pins generally available or physically exposed on a WROOM module. Disconnect external peripherals from GPIO33–37 before using OPI PSRAM. With active PSRAM and the legacy pin map, firmware now runs camera-only recovery: camera, web UI and logs remain available; other hardware stays blocked. Confirm the exact board schematic before rewiring. Camera pin assignments are also board-specific, not universal ESP32-S3 pins.

## Restart and SG92R bring-up

1. Disconnect servo power and the peripherals on GPIO33–37. Boot the board alone. Copy `/logs` and the serial boot output at 115200 baud. A reset before the web server starts requires the serial output.
2. `BROWNOUT` indicates a supply dip; `PANIC` requires the panic/backtrace to distinguish software errors from memory/pin problems. `POWERON` can also follow complete power loss. Do not disable the brownout detector.
3. Power SG92R servos from a suitable regulated external supply (nominal 4.8–5 V; verify your actual servo specification), with capacity for simultaneous starting/stall current. Do not power them from 3.3 V or the ESP regulator. Join supply GND and ESP GND. A capacitor helps transients but cannot replace adequate power/wiring.
4. Support the robot, remove mechanical load/horns, and test one servo first. Existing channels 1/2/3 use GPIO38/39/48; confirm these pads are exposed and not occupied by your board peripherals (GPIO48 is an RGB LED on some boards). Channel 0 on GPIO37 needs a verified replacement on an R8 board. GPIO43 is a possible replacement only if exposed and UART0 TX is not used; update `SERVO_PIN_LH` and its diagnostic label together.
5. Try `/servotest?ch=1&us=1500`, then 1400 and 1600. These small movements establish power/signal operation before testing travel limits. HTTP success means the PWM driver accepted a write, not that a motor moved. The configured 700–2300 µs limits are not a guaranteed SG92R specification. Avoid commanding endpoints until mechanically verified.
6. For the full feature set, create a board-specific pin plan that excludes memory pins. The current camera + separate I2S ports + display + four direct servos consume too many pins for a simple blind remap on many S3 modules. An external servo PWM controller or validated shared audio clocks may be needed. No speculative rewiring has been applied by this change.

References: [Espressif GPIO restrictions](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32s3/api-reference/peripherals/gpio.html), [S3 DevKitC module and pin variants](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.0.html).

---

# BOB V2 — Wiring Guide (ESP32-S3)

**ESP32-S3-N16R8 · 4× servo on direct GPIO · ST7735 1.8" TFT · OV2640 CSI Camera · BNO055 IMU · SPH0645 mic · MAX98357A amp**

This document describes the exact wiring that `BOB_V2/Otto_ESP32S3/Otto_ESP32S3.ino`
expects. Every pin listed here is taken directly from a `#define` in the sketch or
`cam_pins_s3.h`; the sketch reference is given so you can verify rather than trust.

> **Migration note:** BOB V2 targets the **ESP32-S3-N16R8** (16 MB flash, 8 MB OPI PSRAM).
> The classic ESP32 pin map (documented in the root `WIRING.md`) is entirely different —
> GPIO32–36 do not exist on the S3, and many GPIOs 4–18 are occupied by the camera's CSI
> bus. Do not mix the two documents.

---

## 1. Parts list

| Qty | Component | Notes |
|-----|-----------|-------|
| 1 | **ESP32-S3-N16R8** dev module | 16 MB flash, 8 MB OPI PSRAM; *not* the classic ESP32 |
| 4 | SG92R servo | External regulated supply; size for simultaneous starting/stall current |
| 1 | ST7735 1.8" SPI TFT | Red PCB, `INITR_BLACKTAB` init |
| 1 | **OV2640** camera module | Wired to the module's CSI/DVP connector — pins are fixed |
| 1 | **7semi BNO055** 9-axis IMU breakout | Adafruit or compatible; `COM3`/`PS1` pad sets I²C address |
| 1 | SPH0645 I²S MEMS mic | INMP441 also works — see §8 |
| 1 | MAX98357A I²S amplifier | |
| 1 | Speaker, 4 Ω or 8 Ω, ≥ 2 W | |
| 1 | **External 5 V supply, ≥ 2 A** | **Servos only — see §10. This is not optional.** |
| — | Female-to-female Dupont wires | |

---

## 2. Complete pin map

All GPIO numbers refer to the **ESP32-S3** pad labels.

| GPIO | Connects to | Bus / Function | Sketch reference |
|------|-------------|----------------|-----------------|
| 1    | Amp `BCLK`  | I²S port 0 | `I2S_SPK_BCLK` — `Otto_ESP32S3.ino` |
| 2    | Amp `LRC` (WS) | I²S port 0 | `I2S_SPK_LRC` |
| 4    | Camera `SIOD` (SDA) | SCCB | `SIOD_GPIO_NUM` — `cam_pins_s3.h` |
| 5    | Camera `SIOC` (SCL) | SCCB | `SIOC_GPIO_NUM` |
| 6    | Camera `VSYNC` | DVP sync | `VSYNC_GPIO_NUM` |
| 7    | Camera `HREF` | DVP sync | `HREF_GPIO_NUM` |
| 8    | Camera `D2` (Y4) | DVP data | `Y4_GPIO_NUM` |
| 9    | Camera `D1` (Y3) | DVP data | `Y3_GPIO_NUM` |
| 10   | Camera `D3` (Y5) | DVP data | `Y5_GPIO_NUM` |
| 11   | Camera `D0` (Y2) | DVP data | `Y2_GPIO_NUM` |
| 12   | Camera `D4` (Y6) | DVP data | `Y6_GPIO_NUM` |
| 13   | Camera `PCLK` | DVP clock | `PCLK_GPIO_NUM` |
| 14   | Amp `DIN` | I²S port 0 | `I2S_SPK_DIN` |
| 15   | Camera `XCLK` (master clock) | DVP clock | `XCLK_GPIO_NUM` |
| 16   | Camera `D7` (Y9) | DVP data | `Y9_GPIO_NUM` |
| 17   | Camera `D6` (Y8) | DVP data | `Y8_GPIO_NUM` |
| 18   | Camera `D5` (Y7) | DVP data | `Y7_GPIO_NUM` |
| 21   | Mic `DOUT` / `SD` | I²S port 1 | `I2S_MIC_SD` |
| 33   | Mic `BCLK` / `SCK` | I²S port 1 | `I2S_MIC_SCK` |
| 34   | Mic `LRCL` / `WS` | I²S port 1 | `I2S_MIC_WS` |
| 35   | TFT `SDA` / `MOSI` | SPI MOSI | `TFT_MOSI` |
| 36   | TFT `SCL` / `SCK` | SPI CLK | `TFT_CLK` |
| 37   | Servo **Left Hip** signal | LEDC PWM | `SERVO_PIN_LH` |
| 38   | Servo **Left Foot** signal | LEDC PWM | `SERVO_PIN_LF` |
| 39   | Servo **Right Hip** signal | LEDC PWM | `SERVO_PIN_RH` |
| 40   | TFT `CS` | SPI CS | `TFT_CS` |
| 41   | TFT `RES` / `RST` | SPI RST | `TFT_RST` |
| 42   | TFT `DC` / `A0` | SPI DC | `TFT_DC` |
| 45   | IMU `SCL` | I²C | `Wire.begin(47, 45)` in `imuInit()` |
| 47   | IMU `SDA` | I²C | `Wire.begin(47, 45)` in `imuInit()` |
| 48   | Servo **Right Foot** signal | LEDC PWM | `SERVO_PIN_RF` |

### Reserved / do-not-use GPIOs on the ESP32-S3

| GPIO range | Reason |
|------------|--------|
| 4–18 | **Camera CSI bus** — entirely consumed by OV2640 |
| 19, 20 | USB D− / D+ — do not use for peripherals |
| 43, 44 | UART0 TX / RX — leave free for the Serial monitor |
| 0, 3, 45, 46 | Strapping pins — pull to known levels at boot (see §2.1 below) |

### 2.1 Strapping-pin notes — read before powering on

- **GPIO45 (IMU SCL)** is a flash-supply voltage strapping pin. An I²C pull-up can select the wrong voltage at reset unless the module's eFuse configuration overrides it. Verify the exact board before using this pin; do not add a pull-up as a generic boot fix.
- **GPIO46** participates in boot strapping. Keep the board's default bias; do not generically pull it high.
- **GPIO0** selects download mode. Preserve the board's boot circuitry.

---

## 3. Servos → ESP32-S3 (direct GPIO LEDC)

Each servo has three wires. **Signal goes to the ESP32-S3; power does not.**

| Servo | Joint | Signal → ESP32-S3 GPIO | V+ (red) | GND (brown/black) |
|-------|-------|------------------------|----------|-------------------|
| 1 | Left Hip    | **GPIO 37** | External 5 V | External GND |
| 2 | Left Foot   | **GPIO 38** | External 5 V | External GND |
| 3 | Right Hip   | **GPIO 39** | External 5 V | External GND |
| 4 | Right Foot  | **GPIO 48** | External 5 V | External GND |

Internal channel numbering (unchanged from V1):

| Index | Constant | Joint |
|-------|----------|-------|
| 0 | `CH_LH` | Left Hip |
| 1 | `CH_LF` | Left Foot |
| 2 | `CH_RH` | Right Hip |
| 3 | `CH_RF` | Right Foot |

Configured pulse range: 700–2300 µs mapped to 0–180° at 50 Hz (`SERVO_MIN_US` / `SERVO_MAX_US`
in the sketch). The sketch uses the ESP32 core's native **LEDC API** directly —
channels 0–3 on timers 0 and 1 — rather than the `ESP32Servo` library wrapper.
All four channels are attached in the background after the web server starts; initial neutral commands are staggered by 250 ms.

> GPIO37 is reserved on R8 modules. GPIO38/39/48 still require a board-specific check. See the audit above.

---

## 4. ST7735 TFT → ESP32-S3 (SPI / FSPI)

| TFT pin | ESP32-S3 GPIO |
|---------|---------------|
| `VCC` | 3.3 V |
| `GND` | GND |
| `SCL` / `SCK` | **GPIO 36** |
| `SDA` / `MOSI` | **GPIO 35** |
| `RES` / `RST` | **GPIO 41** |
| `DC` / `A0` | **GPIO 42** |
| `CS` | **GPIO 40** |
| `BLK` / `LED` | 3.3 V (or leave floating — most red boards backlight anyway) |

The sketch initialises the display over **hardware SPI** (`TFT_HW_SPI 1`, default).
On the ESP32-S3 the default SPI bus is **FSPI**. The Adafruit ST7735 4-argument
constructor passes `&SPI`, which maps to FSPI on S3 cores; the bus is reconfigured
to GPIOs 35/36 inside `screenSetup()` via `SPI.begin(TFT_CLK, -1, TFT_MOSI, -1)`.
The two `-1`s prevent the driver from grabbing any other default pins as MISO or SS.

> **Power the TFT from 3.3 V, not 5 V.** Most red ST7735 boards have no level
> shifter; 5 V logic can damage the ESP32-S3 input pads.

Set `TFT_HW_SPI` to 0 only as a last resort — bit-banged SPI is orders of magnitude
slower, and one full face redraw (160×128, RGB565) ties up the CPU for hundreds of
milliseconds while the web UI stops responding.

---

## 5. OV2640 Camera → ESP32-S3 (CSI / DVP)

The camera uses the module's dedicated CSI (DVP parallel) connector. These pins are
**fixed by the module's hardware routing** — they cannot be changed.

| Camera signal | ESP32-S3 GPIO | Role |
|---------------|---------------|------|
| `PWDN` | (not connected) | Power-down — not wired on this module |
| `RESET` | (not connected) | Hard reset — not wired on this module |
| `XCLK` | **GPIO 15** | Master clock to OV2640 (20 MHz) |
| `SIOD` | **GPIO 4** | SCCB data (camera I²C) |
| `SIOC` | **GPIO 5** | SCCB clock |
| `D0` / Y2 | **GPIO 11** | Pixel data bit 0 |
| `D1` / Y3 | **GPIO 9** | Pixel data bit 1 |
| `D2` / Y4 | **GPIO 8** | Pixel data bit 2 |
| `D3` / Y5 | **GPIO 10** | Pixel data bit 3 |
| `D4` / Y6 | **GPIO 12** | Pixel data bit 4 |
| `D5` / Y7 | **GPIO 18** | Pixel data bit 5 |
| `D6` / Y8 | **GPIO 17** | Pixel data bit 6 |
| `D7` / Y9 | **GPIO 16** | Pixel data bit 7 |
| `VSYNC` | **GPIO 6** | Vertical sync |
| `HREF` | **GPIO 7** | Horizontal reference |
| `PCLK` | **GPIO 13** | Pixel clock |

All 14 GPIOs above (4–18) are reserved for the camera. **Do not connect any other
peripheral to these pins.** The camera's SCCB bus (`SIOD`/`SIOC` on GPIO 4/5) is
entirely separate from the BNO055 I²C bus (`SDA`/`SCL` on GPIO 47/45).

Camera is initialised in `cameraInit()` using `esp_camera.h` (built-in to the
ESP32 Arduino core — no library install needed). Default resolution: **QVGA
(320×240)**, JPEG quality 12. Endpoints: `GET /cam/stream` (MJPEG) and
`GET /cam/snapshot` (single JPEG).

---

## 6. BNO055 IMU → ESP32-S3 (I²C)

| IMU pin | ESP32-S3 GPIO | Notes |
|---------|---------------|-------|
| `VIN` / `VCC` | 3.3 V | |
| `GND` | GND | |
| `SDA` | **GPIO 47** | Remapped from GPIO 21 (V1) — GPIO 21 is now the mic SD pin |
| `SCL` | **GPIO 45** | Remapped from GPIO 22 (V1) |
| `COM3` / `PS1` | **GND** | Sets I²C address `0x28` (default, `IMU_ADDR` in sketch) |
| `INT` | leave unconnected | |
| `NRST` / `RST` | leave unconnected | |

> **Address:** `COM3`/`PS1` → GND = `0x28` (default). Tie to 3.3 V for `0x29`,
> then update `#define IMU_ADDR 0x28` to `0x29` in the sketch.

Bus runs at **400 kHz** fast mode. The BNO055 is initialised in NDOF (9-DOF sensor
fusion) mode; `bno.setExtCrystalUse(true)` enables the external crystal for better
accuracy (~650 ms extra at boot — normal).

Most BNO055 breakouts carry 4.7 kΩ pull-ups on SDA/SCL. If yours does not, add
4.7 kΩ from each line to 3.3 V.

> **Why these pins?** GPIO 4/5 (the classic I²C defaults) are taken by the camera
> SCCB bus. GPIO 21 (the V1 SDA pin) is now the microphone data pin. GPIO47 is outside the camera range; GPIO45 is a strapping pin requiring board-specific verification.

---

## 7. MAX98357A Amplifier → ESP32-S3 (I²S port 0)

| Amp pin | ESP32-S3 GPIO |
|---------|---------------|
| `VIN` | 5 V (3.3 V works but is quieter) |
| `GND` | GND |
| `BCLK` | **GPIO 1** |
| `LRC` | **GPIO 2** |
| `DIN` | **GPIO 14** |
| `SD` | leave floating (floating = enabled, mono mix) |
| `GAIN` | leave floating (9 dB default) — tie to GND for 12 dB |

Speaker `+` and `−` go to the amp's screw terminal. **Do not connect either speaker
terminal to GND** — the output is bridge-tied (BTL) and grounding one side will
destroy the amp.

Configured as 16 kHz, 16-bit, `I2S_CHANNEL_FMT_RIGHT_LEFT` (stereo pairs). Stereo
format is intentional: `ONLY_LEFT` sends silence on alternate DMA slots and halves
the effective output power. I²S port 0 (`I2S_NUM_0`).

---

## 8. SPH0645 Microphone → ESP32-S3 (I²S port 1)

| Mic pin | ESP32-S3 GPIO |
|---------|---------------|
| `3V` / `VIN` | 3.3 V |
| `GND` | GND |
| `BCLK` | **GPIO 33** |
| `LRCL` / `WS` | **GPIO 34** |
| `DOUT` / `SD` | **GPIO 21** |
| `SEL` | **GND** (selects LEFT slot) |

Runs at 16 kHz, 32-bit frames, `ONLY_LEFT` (`i2s_mic_init()`). I²S port 1
(`I2S_NUM_1`).

`SEL` → GND matches the default `micCh = 0` (LEFT channel). If you tie `SEL` to
3.3 V instead, switch the channel to RIGHT in the UI's Mic Config panel — no
reflash needed.

**Using an INMP441 instead?** Wire it identically, then change two settings in the
Mic Config panel: turn the SPH0645 timing fix **off** and set shift to **16**
(defaults are fix=on, shift=14). The timing fix compensates for the SPH0645
emitting its data one BCLK late relative to standard I²S; the INMP441 does not
need it.

> **Why GPIO 21?** On the classic ESP32 (V1), GPIO 21 was the IMU SDA pin. On the
> S3, the IMU is remapped to GPIO 47 (see §6), freeing GPIO 21 for the mic.
> The V1 mic SD was GPIO 14, but that is now the amp DIN.

---

## 9. mck must stay unassigned

Both I²S configs set `.mck_io_num = I2S_PIN_NO_CHANGE`. Do not fill in an MCK pin —
omitting it causes the driver to default to GPIO 0, and when two I²S ports are both
assigned GPIO 0 as MCK they fight over it. The second `i2s_set_pin()` call returns
error 258 and the mic (or amp) fails silently. The comment explaining this is at
`i2s_speaker_init()` in the sketch.

---

## 10. Power — the part that decides whether this works

```
  External 5 V PSU (>= 2 A)
        |
        +-- (+) ------------------> all 4 servo red wires
        |
        +-- (−) --+--------------> all 4 servo brown/black wires
                  |
                  +--------------> ESP32-S3 GND   <-- COMMON GROUND, REQUIRED

  USB-C / VIN ----------------> ESP32-S3
        |
        +-- ESP32-S3 3.3 V -----> TFT VCC, IMU VCC, mic VIN
        |
        +-- ESP32-S3 5 V (VIN) -> MAX98357A VIN
```

Three rules:

1. **Servos get their own 5 V supply.** Four SG92R servos can pull over 2 A at a
   simultaneous direction reversal. The ESP32-S3's on-board regulator cannot supply
   that; the resulting rail sag causes brownout resets mid-dance.
2. **Grounds must be common.** The external supply's negative rail and the ESP32-S3
   GND must be joined, or the servo signal reference is undefined and the legs jitter
   randomly.
3. **Never feed 5 V into the 3.3 V pin.** That pin is the regulator output, not an
   input.

### Strongly recommended: bulk capacitance

Fit a **1000 µF / 10 V electrolytic** across the servo rail (+ to 5 V, − to GND,
observe polarity), physically close to the servo connectors. A 0.1 µF ceramic in
parallel helps with high-frequency noise.

### PSRAM setting — N16R8 camera recovery uses OPI PSRAM

The `sketch.yaml` now enables OPI PSRAM for the confirmed N16R8:

```yaml
default_fqbn: esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=huge_app,PSRAM=opi,...
```

On R8 variants GPIO33–37 are connected to octal memory. The legacy wiring conflicts with five of these pins. Disabling PSRAM avoids active RAM-bus contention but is not a validated hardware fix. The firmware startup guard leaves diagnostics accessible when active PSRAM conflicts with this map. A board-specific remap is still required.

---

## 11. Bring-up checklist

Wire in this order. Verify each stage before moving on.

**Stage 1 — ESP32-S3 alone**
- [ ] Board boots; Serial at 115200 shows `===== BOOT #1 =====` banner
- [ ] Reset reason reads `POWERON`
- [ ] Hotspot `Bob_robo` / `bob12345` appears; UI loads at `http://192.168.4.1`
- [ ] `http://otto.local` resolves (mDNS working)

**Stage 2 — TFT**
- [ ] 7 wires per §4, VCC on **3.3 V**
- [ ] Boot shows the hacker "ANVYXO" intro, then a face
- [ ] No white border (else try `INITR_GREENTAB`)
- [ ] IP address visible in the bottom status bar

**Stage 3 — Camera**
- [ ] Module inserted into CSI connector (or wired per §5)
- [ ] Log line: `camera: init OK`
- [ ] `GET /cam/snapshot` returns a JPEG image in the browser
- [ ] `GET /cam/stream` shows live MJPEG feed in the UI camera card

**Stage 4 — IMU**
- [ ] 4 wires per §6, `COM3`/`PS1` to GND
- [ ] Log: `imu: BNO055 detected (addr=0x28) calib sys=… gyro=… accel=… mag=…`
- [ ] `GET /imu` returns changing `ax/ay/az` when you tilt the robot
- [ ] BNO055 calibration values (sys/gyro/accel/mag) each rise to 3 after ~30 s of normal movement

**Stage 5 — Servos**
- [ ] External 5 V PSU connected, **grounds joined to ESP32-S3 GND**
- [ ] Bulk capacitor fitted
- [ ] Signal wires on GPIO 37 / 38 / 39 / 48 per §3
- [ ] Log: `setup: servos at neutral`
- [ ] Press **Assembly** in the UI → all four go to a raw 90°
- [ ] Fit the servo horns *now*, with the servos held at 90°

**Stage 6 — Audio**
- [ ] Amp per §7, speaker across the screw terminal only
- [ ] Mic per §8, `SEL` to GND
- [ ] Log shows `i2s speaker: install=0 setpin=0 (0=OK)` and the same for the mic
- [ ] `GET /testtone` produces an audible tone from the speaker
- [ ] Record button captures a waveform that responds to sound (`/miclevel` peak > 0)

**Stage 7 — Full system**
- [ ] Press **Calibrate** in the UI with the robot standing still
- [ ] All six IMU bars drop to ~0 and the orientation cube squares up
- [ ] Lift or shake the robot → scared face, foot flips, feet park at neutral, face returns
- [ ] Lift again without pressing Calibrate → reacts again (auto-recalibrates after each scare)
- [ ] Run a dance → no jitter, no reboot
- [ ] `GET /logs` shows `Boots since power-on : 1`

---

## 12. Diagnosing from the device log

The sketch keeps a **3 KB log ring in RTC memory** that survives resets (not power-off).
After any unexpected restart, open `http://otto.local/logs` before changing anything:

| `This boot's reason` | Meaning | Where to look |
|----------------------|---------|---------------|
| `BROWNOUT (POWER DIP - supply sagged!)` | Servo rail collapsed | §10 — separate PSU, bulk cap, common ground |
| `PANIC (firmware CRASHED)` | Guru Meditation fault | Firmware bug, not wiring |
| `INT_WDT` / `TASK_WDT` | Something blocked `loop()` too long | Often a hung I²C or I²S bus |
| `POWERON` | Normal power-up | Could be a loose power connector |

Lines printed *above* the boot marker are from before the restart — that is the most
useful part.

### Symptom table

| Symptom | Most likely cause |
|---------|-------------------|
| Board won't boot / loops on flash | Memory-pin conflict or incorrect flash settings — verify board and see the pin-map audit |
| Legs twitch at rest | Missing common GND between PSU and ESP32-S3 |
| Reboots during dance, walking is fine | Power delivery — §10 |
| Camera returns 503 | Camera init failed — check CSI connector seating; see log for `camera: init FAIL` |
| Camera stream freezes | Heap pressure; reduce other concurrent loads |
| Screen shows garbage or stays white | `TFT_HW_SPI 0` (bit-bang) — verify GPIOs 35/36/40/41/42 per §4 |
| IMU `not found` at boot | SDA/SCL swapped (GPIO 47/45 order), wrong address, or `COM3`/`PS1` floating |
| `imu: BNO055 not found` (correct wiring) | `COM3`/`PS1` floating — tie firmly to GND or VCC |
| Mic reads all zeros | `SD` not wired to GPIO 21, or `SEL` floating instead of GND |
| Speaker silent after `testtone` | Amp `DIN` not on GPIO 14, or `VIN` not on 5 V |
| `i2s ... setpin=258` | Both I²S ports competing for MCK (GPIO 0) — §9 |
| Servos buzz and get hot | Horns fitted off-neutral; redo Assembly mode — §11 stage 5 |
| Lifting robot does nothing | Not calibrated this boot — press Calibrate in the UI |
| UI reacts once, never again | Recalibration timed out; check IMU still answering (`/imu`) |

---

## 13. Arduino IDE setup

**Board:** `ESP32S3 Dev Module`
**Flash size:** 16 MB
**PSRAM:** `OPI PSRAM` for N16R8 camera recovery. Disconnect peripherals on GPIO33–37; remap them for full operation.
**Partition scheme:** `Huge APP (3MB No OTA / 1MB SPIFFS)`
**Upload speed:** 921600

The CLI default FQBN is recorded in `sketch.yaml`. Explicitly verify the matching Tools settings in Arduino IDE before uploading.

**Libraries** (install via Library Manager):

| Library | Author | Notes |
|---------|--------|-------|
| Adafruit ST7735 and ST7789 Library | Adafruit | |
| Adafruit GFX Library | Adafruit | |
| **Adafruit BNO055** | Adafruit | Choose "Install All" to get Unified Sensor too |
| **Adafruit Unified Sensor** | Adafruit | Pulled in automatically with BNO055 |

`Wire`, `WiFi`, `WebServer`, `ESPmDNS`, `Preferences`, `LittleFS`, `SPI`,
`driver/i2s.h`, and `esp_camera.h` all ship with the ESP32 Arduino core — no
separate install. `ESP8266SAM` and `ESP8266Audio` are **not needed** (SAM TTS is
removed in BOB V2; speech is handled by the `BOB_ROBO_CONVO` Mac server via
`POST /mactts`).

**Before flashing:** WiFi credentials are configured from the web UI — saved credentials override the fallback values in the sketch. On first boot the robot creates the
`Bob_robo` / `bob12345` hotspot; join that, open `http://192.168.4.1`, and enter
your home WiFi details from the settings page.

---

## 14. Web endpoints (BOB V2 additions)

All V1 endpoints remain. BOB V2 adds the camera endpoints:

| Endpoint | Purpose |
|----------|---------|
| `GET /cam/stream` | Live MJPEG stream from the OV2640 |
| `GET /cam/snapshot` | Single JPEG capture |

Both return `503 Service Unavailable` if the camera failed to initialise at boot.
The rest of the endpoint table is unchanged from the root `WIRING.md` §13.

---

## 15. Pin-change summary vs. BOB V1 (classic ESP32)

Quick reference for anyone migrating a wired-up V1 robot.

| Signal | V1 GPIO (classic ESP32) | V2 GPIO (ESP32-S3) | Reason for change |
|--------|-------------------------|--------------------|-------------------|
| TFT CS | 5 | **40** | GPIO 5 → camera SIOC on S3 |
| TFT RST | 32 | **41** | GPIO 32 doesn't exist on S3 |
| TFT DC | 33 | **42** | GPIO33 is reserved for octal memory on R8 variants |
| TFT MOSI | 23 | **35** | |
| TFT CLK | 18 | **36** | GPIO 18 → camera Y7 on S3 |
| Amp BCLK | 25 | **1** | GPIO 25 doesn't exist on S3 |
| Amp LRC | 26 | **2** | GPIO 26 doesn't exist on S3 |
| Amp DIN | 27 | **14** | GPIO 27 doesn't exist on S3 |
| Mic SCK | 32 | **33** | GPIO 32 doesn't exist on S3 |
| Mic WS | 33 | **34** | GPIO33 is reserved for octal memory on R8 variants |
| Mic SD | 14 | **21** | GPIO 14 → amp DIN above |
| Servo Left Hip | 12 | **37** | GPIO 12 → camera Y6 on S3 |
| Servo Left Foot | 13 | **38** | GPIO 13 → camera PCLK on S3 |
| Servo Right Hip | 15 | **39** | GPIO 15 → camera XCLK on S3 |
| Servo Right Foot | 19 | **48** | GPIO 19 → USB D− on S3 |
| IMU SDA | 21 | **47** | GPIO 21 → mic SD above |
| IMU SCL | 22 | **45** | GPIO 22 doesn't exist on S3 |
| Camera | *(separate board)* | **GPIOs 4–18** | Integrated into main firmware |

## Camera-only recovery upload

Upload `Otto_ESP32S3/Otto_ESP32S3.ino` with ESP32S3 Dev Module, 16 MB flash, OPI PSRAM and Huge APP. Disconnect external devices from memory GPIO33–37 first. No camera ribbon change is implied by the pin-conflict log. On startup, expect `setup: camera-only recovery`, then `cam: ready ...` and `setup: recovery camera=OK` if camera initialization succeeds. Open the IP printed in the log and select Start camera or Snapshot. A camera initialization failure is reported in `/logs`. Other hardware endpoints intentionally return HTTP 503 until the pin map is corrected; recovery does not restore movement, audio or the TFT. No flash erase or credential reset is required.
