> Historical servo-only stage. For current wiring use [../WIRING.md](../WIRING.md).

# BOB V2 wiring — ESP32-S3-N16R8 + SG92R servos

This is the current wiring for `BOB_SERVO_ONLY 1` in [Otto_ESP32S3.ino](Otto_ESP32S3/Otto_ESP32S3.ino). Connect only the ESP32, servo supply and servos for this assembly stage. The dashboard, servo tests, trim storage and movement remain available. Display, camera, microphone, speaker and IMU initialization are disabled.

## 1. Move the signal wires

**Disconnect USB and external power before changing wires.** Use the board's printed GPIO numbers, not the physical header positions.

| Servo / joint | Channel | Previous signal GPIO | Current signal GPIO | Firmware constant |
|---|---:|---:|---:|---|
| Left hip | 0 | 37 | **40** | `SERVO_PIN_LH` |
| Left foot | 1 | 38 | **38** | `SERVO_PIN_LF` |
| Right hip | 2 | 39 | **39** | `SERVO_PIN_RH` |
| Right foot | 3 | 48 | **41** | `SERVO_PIN_RF` |

Move the left-hip signal from **37 to 40** and the right-foot signal from **48 to 41**. The other two signals stay on 38 and 39. GPIO40/41 were assigned to the TFT in the old map: keep that display disconnected.

Leave **GPIO33–37 disconnected from external peripherals**. On R8 variants these pins are connected to octal memory. Disabling PSRAM does not make the old wiring a validated design. See [Espressif's GPIO restrictions](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/gpio.html).

## 2. Servo power and ground

Typical SG92R lead colours are shown below; verify the markings on your servo.

| Servo wire | Connect to |
|---|---|
| Orange/yellow signal | Its assigned GPIO in the table above |
| Red positive | External regulated servo supply positive |
| Brown/black ground | External supply negative / ground |
| ESP32 GND | The same external supply ground |

Use a suitable regulated servo supply, nominally **4.8–5 V**, and verify your particular servo's rating. Size the supply for starting/stall current of all connected servos. Do not power servos from ESP32 3.3 V. For initial testing, power the ESP32 through USB and keep the servo supply positive separate from the ESP32 power pins.

```text
USB ───────────────────────────────> ESP32-S3 USB connector

External regulated servo supply
  +4.8–5 V ──┬────────────────────> Left hip RED
             ├────────────────────> Left foot RED
             ├────────────────────> Right hip RED
             └────────────────────> Right foot RED

  GND ───────┬────────────────────> All servo BROWN/BLACK wires
             └────────────────────> ESP32 GND (required)

ESP32 GPIO40 ─────────────────────> Left hip SIGNAL
ESP32 GPIO38 ─────────────────────> Left foot SIGNAL
ESP32 GPIO39 ─────────────────────> Right hip SIGNAL
ESP32 GPIO41 ─────────────────────> Right foot SIGNAL
```

Keep servo power connections short and secure. A 1000 µF capacitor rated at least 10 V across the servo supply near the connectors can help with transients; connect its positive lead to supply positive and negative lead to ground. It does not replace an adequate supply.

## 3. Upload settings

| Setting | Value |
|---|---|
| Sketch | `Otto_ESP32S3/Otto_ESP32S3.ino` |
| Profile in sketch | `#define BOB_SERVO_ONLY 1` |
| Board | ESP32S3 Dev Module |
| Flash size | 16 MB |
| PSRAM | OPI PSRAM |
| Partition scheme | Huge APP (3 MB No OTA / 1 MB SPIFFS) |
| Serial Monitor | 115200 baud |

Reflash after moving the wires: an older firmware still drives the old pins. No full flash erase is needed. On successful initialization, expect:

```text
setup: SERVO-ONLY ready; LH=40 LF=38 RH=39 RF=41; raw centre=1500us
```

Outputs run at 50 Hz. Startup sends raw 1500 µs centre pulses, staggered by 250 ms. Servo-only travel is configured to 1000–2000 µs; physical shaft angles and mechanical limits still need checking on your servos. The UI's degree values are command values, not measured angles.

## 4. Test one servo first

1. With power off, connect only one unloaded SG92R to GPIO40, external power and common ground. Leave horns/linkages detached for the initial test.
2. Power up. Join WiFi **Bob_robo**, password **bob12345**, and open `http://192.168.4.1`. Alternatively use the home-network IP printed in Serial Monitor.
3. Open these URLs one at a time:

   ```text
   http://192.168.4.1/servotest?ch=0&us=1500
   http://192.168.4.1/servotest?ch=0&us=1400
   http://192.168.4.1/servotest?ch=0&us=1600
   ```

4. Expect small movements around centre. A successful HTTP response confirms that PWM was accepted, not that a servo physically moved.
5. Power off before adding each remaining servo. Test channel 1 on GPIO38, channel 2 on GPIO39, and channel 3 on GPIO41 using the same pulse values.
6. Once all respond without resets, use **Assembly** to hold raw centre and fit the horns. Check trims before testing walking or dance.

Movement routines retain their existing blocking behavior: Stop cannot interrupt a routine already executing.

## 5. If something fails

| Symptom | Next check |
|---|---|
| Repeated `RTCWDT_RTC_RST` before the BOB banner | Power off; check for a remaining signal on GPIO37, wrong header position, short or power fault. Disconnect the added wiring and confirm the board boots alone. |
| Board boots but one servo stays still | Verify that channel's GPIO, external servo power and common ground; swap in the known-working servo to separate wiring from motor faults. |
| Restart when a servo moves | Check the reset log, supply voltage under load, power wiring and mechanical binding. A `BROWNOUT` indicates a supply dip. |
| PWM attachment fails | Read `/logs`; verify the flashed profile and GPIO assignments. |
| Camera/audio/display/IMU unavailable | Expected in servo-only mode. Do not connect them yet. |

The earlier boot test showed that disconnecting servo wiring restored boot. That implicates wiring, but does not prove the supply or every servo is working. Firmware compilation has passed; physical operation remains to be verified.

## 6. Later assembly stages

Keep the current profile until servo testing is complete. Adding the TFT will require reassignment because GPIO40/41 are now occupied. The camera, microphone and IMU also need a reviewed board-specific map. Do not restore the previous full-system wiring or disable `BOB_SERVO_ONLY` as a shortcut.

The [archived full-system guide](archive/WIRING-full-system-legacy.md) is retained only as historical reference and includes known invalid assignments. The root `WIRING.md` describes the older classic ESP32, not this S3 board.
