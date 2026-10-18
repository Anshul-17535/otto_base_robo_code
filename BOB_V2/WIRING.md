# BOB V2 wiring — ESP32-S3-N16R8 peripherals

This is the active `BOB_VISUAL_BRINGUP 1` profile in [Otto_ESP32S3.ino](Otto_ESP32S3/Otto_ESP32S3.ino), for ESP32-S3-N16R8. All four servo pins, screen, camera, SPH0645 microphone, MAX98357A speaker and MPU-6500/9250 IMU initialize automatically at boot. Persistent clip storage remains disabled; short mic recordings download as WAV from RAM.

**Recorded hardware:** one SG92R (user called it "923") and three MG90S. The user verified all four working after replacing three faulty motors. Which joint uses the SG92R is not yet recorded.

The ST7735 automatically shows a black-and-green terminal intro and types BOB before WiFi connection attempts. After connecting, a five-second network splash shows the home WiFi name and assigned IP (or hotspot IP when unavailable). The face then returns with the reachable IP in the bottom bar. Reconnection and address changes refresh the splash automatically. The camera initializes after the servos; its live stream opens from the dashboard **Start Camera** button.

## 1. Working servo connections

**Disconnect USB and external power before changing wires.** Use the board's printed GPIO numbers, not physical header positions.

| Servo / joint | Channel | Firmware constant | Default GPIO |
|---|---:|---|---:|
| Left hip  | 0 | `SERVO_PIN_LH` | **40** |
| Left foot | 1 | `SERVO_PIN_LF` | **38** |
| Right hip | 2 | `SERVO_PIN_RH` | **39** |
| Right foot| 3 | `SERVO_PIN_RF` | **41** |

**Live saved map observed 2026-09-27 at 192.168.1.9:** LH=38, LF=40, RH=39, RF=41 (`GET /servomap`); all trims zero. This user-saved hip/foot swap takes precedence over the factory defaults below. No wiring or mapping was changed during Motion Studio work.

**GPIO reassignment from the UI:** The dashboard (main page) now includes a **Servo GPIO Pin Map** card. Open the robot's IP in your browser, scroll to the card, pick a new GPIO from the dropdown for each joint, and tap **Save Pin Map to Flash**. Permitted GPIOs are 38, 39, 40 and 41 — each used exactly once. The firmware swaps the logical roles live (no reflash); the mapping is preserved across power cycles in NVS. Stop all motion and recover from any active lift reaction before saving a new map.

Leave **GPIO33–37 disconnected from external peripherals**. On R8 variants these connect to octal PSRAM. Do not use the legacy GPIO37/48 servo map.

**GPIO40/41 belong to the servos, not the TFT.** The TFT uses GPIO1/2/21/42/47 (section 2).

## 2. ST7735 1.8-inch screen connections

Power off before adding the screen. The code assumes the existing ST7735 160×128 display in landscape (`INITR_BLACKTAB`).

| TFT label | ESP32 connection | Firmware constant |
|---|---|---|
| VCC | **3.3 V** | — |
| GND | **GND** | — |
| SDA / DIN / MOSI | **GPIO1** | `TFT_MOSI` |
| SCL / SCK / CLK | **GPIO2** | `TFT_CLK` |
| CS | **GPIO21** | `TFT_CS` |
| DC / A0 | **GPIO42** | `TFT_DC` |
| RES / RST | **GPIO47** | `TFT_RST` |
| BLK / LED | **3.3 V** (module with onboard backlight limiting) | — |

On this display, SDA/SCL labels mean SPI data/clock, not I²C. Hardware SPI runs at 8 MHz. **GPIO1 and GPIO2 are TFT MOSI and SCK. GPIO21 is TFT CS; GPIO42 is DC; GPIO47 is reset. Do not connect any other peripheral to GPIO1 or GPIO2.**

## 3. Camera connections

Keep the existing camera ribbon/connector wiring. Camera pins are fixed by the module wiring and cannot be remapped. The firmware map from `cam_pins_s3.h`:

| Camera signal | GPIO |
|---|---:|
| XCLK | 15 |
| SIOD / SCCB SDA | 4 |
| SIOC / SCCB SCL | 5 |
| D0 / Y2 | 11 |
| D1 / Y3 | 9 |
| D2 / Y4 | 8 |
| D3 / Y5 | 10 |
| D4 / Y6 | 12 |
| D5 / Y7 | 18 |
| D6 / Y8 | 17 |
| D7 / Y9 | 16 |
| VSYNC | 6 |
| HREF | 7 |
| PCLK | 13 |
| PWDN / RESET | Not assigned (`-1`) |

The camera uses LEDC channel 7/timer 3 for XCLK; servos use channels 0–3/timers 0–1. Camera initialization runs automatically at boot; stream opens on request from the UI.

After upload:
- Snapshot: `http://<IP>/cam/snapshot`
- Stream: `http://<IP>:81/stream` or **Start camera** in dashboard.
- Diagnosis: `http://<IP>/logs` and `/hw`.

## 4. SPH0645 microphone

Power the mic from ESP32 **3.3 V**, not 5 V. Power off before wiring.

| SPH0645 label | ESP32-S3 connection | Direction |
|---|---|---|
| 3V / VDD | **3.3 V** | Power |
| GND | **GND** | Common ground |
| BCLK / SCK | **GPIO43** | ESP32 to mic (I²S bit clock) |
| LRCL / WS | **GPIO44** | ESP32 to mic (I²S word select) |
| DOUT / SD | **GPIO48** | Mic to ESP32 (I²S data) |
| SEL | **GND** | Select LEFT I²S channel |

GPIO43 (`U0TXD`) and GPIO44 (`U0RXD`) are repurposed as I²S mic clock/WS — **not UART**. USB CDC carries serial logs; UART0 is not used for serial monitor in this firmware profile.

After wiring, open the dashboard **Audio Diagnostics** card → **Mic Meter: ON**. Talk or clap: the level bar should rise. If readings stay zero, try **Auto-Scan All 8 Mic Configs** in the UI.

### Speaker wiring — GPIO3/45/14

**Keep amplifier DIN on GPIO14.** GPIO3 (BCLK) and GPIO45 (LRC) are boot-strapping pins; the amp inputs must remain high-impedance during reset.

| MAX98357A label | ESP32-S3 connection | Note |
|---|---|---|
| BCLK / BCK | **GPIO3** | Strap pin |
| LRC / LCK / WS | **GPIO45** | Strap pin |
| DIN | **GPIO14** | Speaker data |
| VIN | **5 V** | Amp power (not ESP32 3.3 V) |
| GND | **GND** | Shared ground |
| **GAIN** | **GND** | **Critical — must be wired. Floating = +15 dB internal gain → amp clips on every signal. GND = +3 dB (clean). For more volume: 100kΩ to VIN = +6 dB, 10kΩ to VIN = +9 dB, VIN direct = +12 dB.** |
| SPK+ / SPK− | Speaker terminals | Connect speaker only to amp outputs |

Test with **Speaker Test Tone** in Audio Diagnostics after flashing.

### MPU-9250 / GY-6500 IMU wiring

Firmware reads `WHO_AM_I`: `0x70` = MPU-6500, `0x71` = MPU-9250, `0x73` = MPU-9255. Uses accel + gyro only; no magnetometer or absolute yaw. The detected chip and I²C address appear in `/hw`, `/imu` and `/logs`.

| MPU module label | ESP32-S3 connection | Note |
|---|---|---|
| VCC / VDD | **3.3 V** | Keeps I²C pull-ups at 3.3 V logic |
| GND | **GND** | Shared ground |
| SDA | **GPIO46** | I²C data; **disconnect for ROM bootloader upload** |
| SCL | **GPIO0** | I²C clock; ESP32 boot strap, normally high |
| AD0 / ADO | **GND** | Selects address `0x68` |
| CS / NCS | **3.3 V** (if present) | Selects I²C over SPI |
| INT, XDA, XCL | Leave disconnected | Not used |

**Upload procedure:** power off, disconnect IMU SDA46 and SCL0 signal wires, upload, power off, reconnect, power on. GPIO46 must be low/floating for ROM download; the I²C pull-up prevents this.

Complete signal allocation:

| Peripheral | GPIOs |
|---|---|
| Servos (LH/LF/RH/RF) | 40, 38, 39, 41 (default; remappable in UI to any of 38–41) |
| ST7735 screen | MOSI1, SCK2, CS21, DC42, RST47 |
| OV2640 camera | 4–13, 15–18 |
| SPH0645 mic | BCLK43, WS44, DOUT48 |
| MAX98357A amp | BCLK3, LRC45, DIN14 |
| MPU IMU | SDA46, SCL0 |
| Native USB | 19, 20 (reserved) |
| Flash / OPI PSRAM | 26–37 (reserved) |

After flashing, open `http://<IP>/hw`. The IMU row should report `MPU-6500` or `MPU-9250` at `0x68`/`0x69`. Open `/imu` and tilt the robot: `pitch` and `roll` should change.

## 5. Servo power and ground

| Servo wire | Connect to |
|---|---|
| Orange/yellow signal | Its assigned GPIO (see section 1 or the Pin Map UI) |
| Red positive | External regulated servo supply positive (4.8–5 V) |
| Brown/black ground | External supply negative / ground |
| ESP32 GND | The same external supply ground |

Do not power servos from ESP32 3.3 V. Size the supply for starting/stall current of all four servos.

```text
USB ───────────────────────────────> ESP32-S3 USB connector

External regulated servo supply
  +4.8–5 V ──┬────────────────────> Left hip RED
             ├────────────────────> Left foot RED
             ├────────────────────> Right hip RED
             └────────────────────> Right foot RED

  GND ───────┬────────────────────> All servo BROWN/BLACK wires
             └────────────────────> ESP32 GND (required)

ESP32 GPIO40 ─────────────────────> Left hip SIGNAL   (default; see UI)
ESP32 GPIO38 ─────────────────────> Left foot SIGNAL  (default; see UI)
ESP32 GPIO39 ─────────────────────> Right hip SIGNAL  (default; see UI)
ESP32 GPIO41 ─────────────────────> Right foot SIGNAL (default; see UI)
```

A 1000 µF capacitor (≥10 V) across the servo supply near the connectors helps with transients.

## 6. Upload settings

### 6a. First flash via USB (required once to set partition scheme)

| Setting | Value |
|---|---|
| Sketch | `Otto_ESP32S3/Otto_ESP32S3.ino` |
| Active profile | `BOB_VISUAL_BRINGUP 1`, `BOB_MIC_TEST 1`, `BOB_SPEAKER_TEST 1`, `BOB_IMU_TEST 1` |
| Board | ESP32S3 Dev Module |
| Flash size | 16 MB |
| PSRAM | OPI PSRAM |
| **Partition scheme** | **Minimal SPIFFS (1.9MB APP with OTA / 190KB SPIFFS)** ← changed from huge_app |
| USB Mode | Hardware CDC and JTAG recommended |
| USB CDC On Boot | Enabled for serial logs |

> **Why the partition change?** The old "Huge APP (3MB No OTA)" scheme has a single app slot — OTA has nowhere to write a new image. `min_spiffs` splits the flash into two ~1.9 MB app slots so the ESP32 can receive a new image into the inactive slot and reboot into it. After this one USB flash you never need a cable again.
> If the compiled sketch grows beyond 1.9 MB in the future, use `custom_16mb_ota.csv` (in the sketch folder) which gives two 3 MB slots.

Disconnect IMU SDA/SCL before flashing. Successful boot log:

```text
setup: screen initialized before WiFi (MOSI=1 CLK=2 CS=21 DC=42 RST=47)
cam: ready PID=... (QVGA JPEG, PSRAM x2)
i2s mic: install=0 setpin=0 ch=0 fmt=0 ...
imu: MPU-6500 detected (WHO_AM_I=0x70 addr=0x68 SDA=46 SCL=0)
OTA: ArduinoOTA ready on port 3232 (password: ***set***)
setup: web server up - http://192.168.1.15  home=192.168.1.15  OTA port 3232
setup: VISUAL ready; servos=4/4 screen=initialized camera=OK mic=OK speaker=enabled imu=MPU-6500
```

### 6b. WiFi OTA (every subsequent flash — no cable, no IMU disconnect)

**Three methods — pick whichever fits your workflow:**

#### Method 1 — Browser drag-drop (easiest)

1. Compile the sketch in Arduino IDE: **Sketch → Export Compiled Binary** (produces `Otto_ESP32S3.ino.bin`)
2. Open `http://<robot_ip>/ota` in a browser
3. Enter the OTA password (default: `bob12345`)
4. Click **Choose File**, select the `.bin`, click **Upload & Reboot**
5. Watch the progress bar; the robot reboots automatically when done

#### Method 2 — curl (from Mac/Linux terminal)

```bash
# Replace 192.168.1.15 with your robot's IP and set the correct .bin path
curl -X POST http://192.168.1.15/firmware \
     -H "X-OTA-Password: bob12345" \
     -F "firmware=@/path/to/Otto_ESP32S3.ino.bin" \
     --progress-bar
```

The robot reboots on success. Check `/logs` if it fails.

#### Method 3 — arduino-cli (IDE-compatible espota protocol)

```bash
# Build the sketch first
arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=min_spiffs,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc \
    BOB_V2/Otto_ESP32S3/

# Upload via WiFi (port 3232, mDNS name or IP)
arduino-cli upload \
    --fqbn esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=min_spiffs,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc \
    -p anvyxo-robo.local:3232 \
    --input-file Otto_ESP32S3.ino.bin
```

In Arduino IDE: **Tools → Port → Network ports → anvyxo-robo at 192.168.1.15** then Upload as normal.

> **IMU wires:** WiFi OTA does NOT require disconnecting the IMU. Only USB ROM-mode flashing needs GPIO46 low. OTA writes to flash via the running firmware — the IMU can stay wired.

## Assembled idle pose (user confirmed 2026-09-27)

The user initially reported logical idle **LH90 / LF90 / RH90 / RF31**; the latest post-restart live pose is **LH80 / LF65 / RH90 / RF45**. Verify the assembled stance before adopting either. RF31 is intentional, not a trim error. IMU calibration was confirmed near zero at this pose. Preserve the saved GPIO mapping above. Updated Motion Studio API v2 can capture this idle pose without motion and store a narrow range around it. A saved matching profile controls startup and Stand; electrical calibration/assembly commands still explicitly drive 90° and must not be used as idle on this assembled robot. No new GPIOs are assigned. See [MOTION_STUDIO.md](MOTION_STUDIO.md).

## 7. Servo test reference

1. Power off; connect one unloaded servo to GPIO40, external power and common ground. Leave horns/linkages detached.
2. Power up. Join WiFi **Bob_robo** / **bob12345**. Open `http://192.168.4.1`.
3. Use the **Motor Control** sliders to send individual positions. Or test via URL:
   ```
   http://192.168.4.1/servotest?ch=0&us=1500
   http://192.168.4.1/servotest?ch=0&us=1400
   http://192.168.4.1/servotest?ch=0&us=1600
   ```
4. A successful HTTP response confirms PWM was accepted; verify the servo physically moved.
5. Power off before adding each remaining servo. Test channel 1 on GPIO38, channel 2 on GPIO39, channel 3 on GPIO41.
6. Once all respond, use **Assembly** (Hold Raw 90°) to fit horns. Check trims before walking or dance.

**Motion Studio** at `http://<IP>/motion` provides nonblocking four-joint sequences, NVS movement storage and optional MPU PID posture correction. See [MOTION_STUDIO.md](MOTION_STUDIO.md).

### IMU-driven movement commands

Available via `GET /move?cmd=<name>` with `BOB_IMU_TEST 1` and a calibrated IMU:

| Command | Function | Duration |
|---|---|---|
| `imu_lean` | Hips/feet follow live pitch and roll | 4 s |
| `imu_balance` | Compensates against tilt | 4 s |
| `imu_tiltwalk` | Walk stride scales with forward pitch | 5 steps |
| `imu_swagger` | Side strut with amplitude from roll | 6 beats |
| `imu_sway` | Pendulum sway with gyro-Z tempo | 6 beats |

`GET /imupose` returns `{pitch, roll, hipBias, footBias, calOk, ax, ay, az}`. Calibrate the IMU first.

## 8. If something fails

| Symptom | Next check |
|---|---|
| Repeated `RTCWDT_RTC_RST` before BOB banner | Power off; check for a signal on GPIO37, wrong header position, short or power fault. Disconnect added wiring and confirm the board boots alone. |
| Board boots but one servo stays still | Verify that channel's GPIO, external servo power and common ground; swap in the known-working servo to separate wiring from motor faults. |
| Restart when a servo moves | Check the reset log, supply voltage under load, wiring and mechanical binding. `BROWNOUT` = supply dip. |
| PWM attachment fails | Read `/logs`; verify the flashed profile and GPIO assignments. |
| Screen backlight on but no face | Verify all five TFT signal wires (CS21, RST47, DC42, MOSI1, CLK2). Software cannot detect a disconnected write-only TFT. |
| Screen completely dark | Check VCC, GND and backlight wiring. |
| Camera unavailable | Read `cam: init FAILED` in `/logs`; verify OPI PSRAM and existing ribbon/board map. |
| Speaker test silent | Confirm `BOB_SPEAKER_TEST 1`, amp VIN/GND, speaker on SPK+/SPK−, and **BCLK3/LRC45/DIN14**. Check `/logs` for `i2s speaker: install=0 setpin=0`. |
| IMU unavailable | Check 3.3 V, common ground, SDA46, SCL0, AD0 low, CS/NCS high if present. Check `/logs` for WHO_AM_I. |
| Upload fails with IMU connected — `Wrong boot mode detected (0x4)` | Power off and disconnect **both** IMU signal wires (SDA46 and SCL0) before uploading. This is a permanent constraint of the GPIO46/0 IMU assignment. |
| Mic meter always reads zero | Check 3.3 V, GND, SEL→GND, BCLK43, WS44, DOUT48. Use the **Auto-Scan All 8 Mic Configs** button in Audio Diagnostics. |
| Servo Pin Map UI saves but servos don't match joints | The UI remaps logical roles live. If roles are swapped, use the UI to correct the assignment and save again. No reflash needed. |

## 9. Servo GPIO Pin Map — browser UI

The main dashboard at `http://<IP>/` includes a **Servo GPIO Pin Map** card (below the Servo Trim Calibration card). It:
- Fetches the current assignment from the robot via `GET /servomap`
- Shows a dropdown for each joint (Left Hip, Left Foot, Right Hip, Right Foot) with the four allowed GPIOs
- Validates that all four GPIOs are unique before enabling the **Save** button
- POSTs `{pins:[lh,lf,rh,rf]}` to `POST /servomap`
- The firmware swaps logical servo roles live (no reflash) and saves the new layout to NVS (`bobservo` namespace)

The `GET /servomap` response includes: `{pins, allowed, trim, pos, saved}`. The allowed list is defined in `servo_mapping.h` (`ALLOWED_PINS`). To add a new safe GPIO (if peripheral wiring changes), edit `ALLOWED_PINS` there and recompile.

The `POST /servomap` endpoint is blocked while Motion Studio is armed (`motion.armed`), during a lift reaction, or while scared mode is active. Stop all motion before remapping.

## 10. Later assembly stages

Keep `BOB_VISUAL_BRINGUP 1`. The MAX98357A retains GPIO3/45/14 and the IMU uses GPIO46/0. Do not switch to the historical full-system profile.

The [archived full-system guide](archive/WIRING-full-system-legacy.md) contains known invalid assignments. The [servo-only guide](archive/WIRING-servo-only.md) records the previous test stage. The root `WIRING.md` describes the older classic ESP32, not this S3 board.

## Boot and microphone checks

After uploading, power-cycle without a browser: expect green terminal lines, BOB, network status for five seconds, then the normal face. With the router unavailable, expect the hotspot IP (`192.168.4.1`). When home WiFi reconnects, expect a new splash with the assigned IP. These physical/offline scenarios require checking on the robot; a successful compile does not verify display or microphone output.

## Conversation server audio

`BOB_ROBO_CONVO` uses the 1–3 second RAM `/record` endpoint for Bob mic capture and multipart `POST /mactts` for speech. The current sketch enables `/mactts` when `BOB_SPEAKER_TEST=1`; upload it before using conversation replies. Choose Bob mic or the Mac's default input at `http://localhost:8090`.
