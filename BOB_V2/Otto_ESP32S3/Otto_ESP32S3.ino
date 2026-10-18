
/*
 * ============================================================
 *  Otto DIY — All-in-One Firmware  (BOB V2)
 *  ESP32-S3-N16R8 + ST7735 1.8" TFT + OV2640 CSI Camera
 *
 *  Board (Arduino IDE):
 *    "ESP32S3 Dev Module"
 *    Flash: 16MB   PSRAM: see wiring guide; legacy pins conflict with OPI RAM
 *    Partition: Huge APP (3MB No OTA / 1MB SPIFFS)
 *    Upload speed: 921600
 *
 *  WiFi    : boots AP "Anvyxo_robo" / bob12345 (always on)
 *            also joins home WiFi if credentials saved in NVS
 *  Open    : http://otto.local  — or IP on TFT, port 80
 *
 *  Features:
 *    • D-pad movement (forward / backward / left / right)
 *    • Dance (star eyes + grin, bouncy)
 *    • Per-servo trim calibration (±1° or ±5° buttons)
 *    • Trim saved to flash (NVS) — survives power-off
 *    • Assembly mode — raw 90° for horn fitting
 *    See BOB_V2/WIRING.md before connecting an N16R8 module.
 *  I2S Speaker (MAX98357A): active test output; dedicated map:
 *    BCLK=GPIO3, LRC=GPIO45, DIN=GPIO14 (3/45 are boot-strapping pins).
 *  I2S Microphone legacy fallback (inactive while BOB_MIC_TEST=1):
 *    SCK=GPIO33, WS=GPIO34, SD=GPIO21
 *
 *  Camera (OV2640 CSI — fixed by module wiring):
 *    XCLK=15  SIOD=4  SIOC=5
 *    Y2-Y9 = 11,9,8,10,12,18,17,16
 *    VSYNC=6  HREF=7  PCLK=13
 *    (defined in cam_pins_s3.h)


 *    • Motion tuning: speed, hip swing, foot tilt
 *    • ST7735 TFT — 7 animated Bob expressions + dance mode
 *    • Hacker boot screen

 *    • Auto-blink, look/wink idle, per-expression animations
 *    • Live log in browser (/logs)
 *    • SPH0645 I2S microphone — Record button streams audio
 *    • MAX98357A I2S amplifier — test tone + beep speech in bring-up profile
 *    • OV2640 camera — /cam/stream (MJPEG) + /cam/snapshot (JPEG)
 *    • TTS: handled by BOB_ROBO_CONVO Mac server (no on-device SAM)
 *
 *  Active servo channels (core LEDC API, channels 0-3 = timers 0-1):
 *    CH0 → Left Hip (GPIO40)    CH1 → Left Foot (GPIO38)
 *    CH2 → Right Hip (GPIO39)   CH3 → Right Foot (GPIO41)
 *  Active TFT: MOSI=1 CLK=2 CS=21 DC=42 RST=47. Camera pins unchanged.
 *  SPH0645 test: BCLK=43 WS=44 SD=48; USB CDC carries Serial logs.
 *  I²C MPU-6500/9250: SDA=GPIO46, SCL=GPIO0; AD0 low=0x68, high=0x69.
 *    GPIO0 is a boot strap: keep it pulled high by the I²C bus at reset.
 *    Disconnect IMU SDA/SCL before flashing; GPIO46 must be low for download.
 *
 *  I2S Speaker (MAX98357A): test output BCLK=3, LRC=45, DIN=14.
 *    GPIO3/45 are strapping pins; see BOB_V2/WIRING.md before hookup.
 *  I2S Microphone (SPH0645, active test map):
 *    SCK=GPIO43, WS=GPIO44, SD=GPIO48
 *
 *  Camera (OV2640 CSI — fixed by module wiring):
 *    XCLK=15  SIOD=4  SIOC=5
 *    Y2-Y9 = 11,9,8,10,12,18,17,16
 *    VSYNC=6  HREF=7  PCLK=13
 *    (defined in cam_pins_s3.h)
 *
 *  !! CAMERA PINS (4-18) ARE RESERVED — do NOT reassign !!
 *
 *  Libraries needed (Library Manager):
 *    • Adafruit ST7735 and ST7789 Library
 *    • Adafruit GFX Library
 *    (esp_camera, driver/i2s — built-in ESP32 Arduino core)
 * ============================================================
 */

// Current assembly stage: four servos + ST7735 + camera + audio + MPU IMU.
// Motors: one SG92R and three MG90S (joint assignment not yet recorded).
// Keep 1: the historical full-system map below is not valid for this wiring.
#define BOB_VISUAL_BRINGUP 1
#define BOB_MIC_TEST 1
#define BOB_SPEAKER_TEST 1
#define BOB_IMU_TEST 1

#include <Wire.h>
#include "motion_engine.h"
#include "movements_library.h"
#include <LittleFS.h>      // sound-clip storage on the ESP32's own flash
// ── Camera (OV2640 via CSI/DVP) ──────────────────────────────
// Keep the camera sensor_t typedef isolated from optional sensor libraries.
#define sensor_t camera_sensor_t
#include "esp_camera.h"
#undef sensor_t
#include "cam_pins_s3.h"   // ESP32-S3-N16R8 CSI pin definitions

// ── MPU-6500 / MPU-9250 IMU ───────────────────────────────────
// GY-6500 boards may contain either chip. Probe WHO_AM_I at 0x68/0x69.
// Both chips use the same accel/gyro registers; no external library needed.
#define IMU_SDA 46
#define IMU_SCL 0
#include <WiFi.h>
#include <ESPmDNS.h>       // lets you open http://otto.local instead of the IP
#include <WebServer.h>
#include <ArduinoOTA.h>    // WiFi OTA: upload firmware via arduino-cli / IDE
#include <Update.h>        // HTTP OTA: POST /firmware binary upload
#include "esp_http_server.h"   // separate server on :81 for the camera stream
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <driver/i2s.h>   // legacy I2S API (i2s_driver_install / i2s_read / i2s_write)
// NOTE: soc/i2s_reg.h and the SPH0645 timing-fix registers are classic-ESP32 only.
// On the ESP32-S3 those registers do not exist — the fix block is removed.
// TTS is handled externally by the BOB_ROBO_CONVO Mac server via POST /mactts.
// ESP8266SAM / AudioOutput.h are not needed and not compiled in.
#include <esp_task_wdt.h>
#include <ESPAsyncWebServer.h>  // async HTTP for /mactts — runs on Core 0, never blocks loopTask

// =============================================================
//  WiFi / Web server  (declared HERE, before the audio code:
//  the I2S helpers below reference `server`)
// =============================================================
// The robot always boots its own hotspot (Anvyxo_robo / bob12345) so you can
// reach the UI even on first power-up. If a home WiFi SSID + password have
// been saved to NVS it also tries to join that network; when it succeeds it
// stays in AP+STA mode so the hotspot stays up and the new IP shows on screen.
// Home WiFi credentials are never hard-coded — configure them from the UI.

// ── Built-in AP (always on) ───────────────────────────────────
const char* AP_SSID = "Anvyxo_robo";
// WPA2 needs 8+ characters: core 3.x silently refuses a shorter password and
// the hotspot never comes up (the old "bob1234" did exactly that).
const char* AP_PASS = "bob12345";

// ── Home WiFi — loaded from NVS at boot ──────────────────────
// These are only the FALLBACK used when nothing has been saved from the UI;
// loadWifiCreds() replaces them with the NVS values when present.
String wifiHomeSsid = "Airtel_kuma_9602";
String wifiHomePass = "air71417";
bool   wifiHomeOk   = false;   // true = currently joined to home network

#define WIFI_JOIN_TIMEOUT_MS 15000
#define WEB_PORT             80      // UI is served here: http://<ip>:80
#define TTS_PORT             8080    // async TTS endpoint: POST http://<ip>:8080/mactts

WebServer server(WEB_PORT);
AsyncWebServer ttsServer(TTS_PORT);  // handles /mactts off-loopTask on Core 0
String ottoIP  = "";   // AP IP (always 192.168.4.1)
String homeIP  = "";   // STA IP when joined to home WiFi (else "")
bool   apMode  = true; // always true now — AP is always up

// =============================================================
//  Device log — ring buffer in RTC memory, SURVIVES RESTARTS
//  (everything except full power-off). After a crash/brownout the
//  lines logged just before the reset are still readable at /logs.
// =============================================================
#include <esp_attr.h>
#include <esp_system.h>
#include <stdarg.h>

#define DLOG_SIZE  3072
#define DLOG_MAGIC 0x0770B0B1UL
bool serialLogEnabled = true;

typedef struct {
  uint32_t magic;
  uint32_t head;    // next write position
  uint32_t used;    // valid bytes (saturates at DLOG_SIZE)
  uint32_t boots;   // boot counter since last power-on
  char     buf[DLOG_SIZE];
} DLogRing;

RTC_NOINIT_ATTR DLogRing dlog;   // RTC slow memory — not cleared on reset

static inline void dlogPutc(char c) {
  dlog.buf[dlog.head] = c;
  dlog.head = (dlog.head + 1) % DLOG_SIZE;
  if (dlog.used < DLOG_SIZE) dlog.used++;
}

// Log one line: prints to Serial AND appends to the crash-surviving ring.
void dlogPrintf(const char* fmt, ...) {
  char line[168];
  unsigned long ms = millis();
  int n = snprintf(line, sizeof(line), "[%6lu.%03lu] ", ms / 1000, ms % 1000);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line + n, sizeof(line) - n, fmt, ap);
  va_end(ap);
  if (serialLogEnabled) Serial.println(line);
  for (const char* p = line; *p; p++) dlogPutc(*p);
  dlogPutc('\r'); dlogPutc('\n');
}

// =============================================================
//  Hardware status — what came up at boot, shown in the web UI
// =============================================================
// The web server starts BEFORE any peripheral is touched and the peripherals
// are brought up afterwards in a background task (hwInitTask). So the UI is
// reachable even with nothing plugged in, and GET /hw reports each part:
//   OK      = detected and working            (green)
//   FAIL    = not found / init failed          (red)
//   NOCHECK = driver started, but the part has no way to answer back, so
//             presence can't be verified (screen, servos, speaker)  (grey)
enum HwState : uint8_t { HW_PENDING = 0, HW_OK, HW_FAIL, HW_NOCHECK };
enum HwId : uint8_t { HW_PSRAM, HW_WIFI_AP, HW_WIFI_HOME, HW_FLASH, HW_SERVOS,
                      HW_SCREEN, HW_SPEAKER, HW_MIC, HW_CAMERA, HW_IMU, HW_COUNT };
struct HwItem { const char* key; const char* name; volatile HwState st; char info[64]; };
HwItem hw[HW_COUNT] = {
  {"psram",  "PSRAM",        HW_PENDING, ""},
  {"ap",     "WiFi hotspot", HW_PENDING, ""},
  {"home",   "Home WiFi",    HW_PENDING, ""},
  {"flash",  "Flash storage",HW_PENDING, ""},
  {"servos", "Servos",       HW_PENDING, ""},
  {"screen", "Screen",       HW_PENDING, ""},
  {"spk",    "Speaker",      HW_PENDING, ""},
  {"mic",    "Microphone",   HW_PENDING, ""},
  {"cam",    "Camera",       HW_PENDING, ""},
  {"imu",    "IMU (MPU-6500/9250)", HW_PENDING, ""},
};
volatile bool hwInitDone = false;
const char* hwInitError = nullptr; // startup failure; UI/logs remain accessible
const char* volatile hwStep = "boot";   // which init step is running (shows a hang)

void hwSet(HwId id, HwState st, const char* fmt = "", ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(hw[id].info, sizeof(hw[id].info), fmt, ap);
  va_end(ap);
  hw[id].st = st;
}

// Human-readable reset reason — THE key line for debugging restarts.
const char* resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "POWERON (normal power-up / EN button)";
    case ESP_RST_EXT:      return "EXT (external reset pin)";
    case ESP_RST_SW:       return "SW (esp_restart / software reset)";
    case ESP_RST_PANIC:    return "PANIC (firmware CRASHED - Guru Meditation)";
    case ESP_RST_INT_WDT:  return "INT_WDT (interrupt watchdog - code blocked too long)";
    case ESP_RST_TASK_WDT: return "TASK_WDT (task watchdog - loop starved)";
    case ESP_RST_WDT:      return "WDT (other watchdog)";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP wake";
    case ESP_RST_BROWNOUT: return "BROWNOUT (POWER DIP - supply sagged!)";
    case ESP_RST_SDIO:     return "SDIO";
    default:               return "UNKNOWN";
  }
}

// Call FIRST in setup(): validates/initialises the ring, stamps a boot
// marker with the reset reason of the restart that just happened.
void dlogBoot() {
  esp_reset_reason_t rr = esp_reset_reason();
  bool cold = (dlog.magic != DLOG_MAGIC) || (rr == ESP_RST_POWERON);
  if (cold || dlog.head >= DLOG_SIZE || dlog.used > DLOG_SIZE) {
    dlog.magic = DLOG_MAGIC;
    dlog.head  = 0;
    dlog.used  = 0;
    dlog.boots = 0;
  }
  dlog.boots++;
  dlogPrintf("===== BOOT #%lu ===== reason: %s",
             (unsigned long)dlog.boots, resetReasonStr(rr));
  if (!cold && dlog.boots > 1)
    dlogPrintf("(lines ABOVE the boot marker are from BEFORE this restart)");
}

// GET /logs — dump the whole ring as plain text (+ live stats header).
// GET /logs?clear=1 — wipe the ring.
void handleLogs() {
  if (server.hasArg("clear")) {
    dlog.head = 0; dlog.used = 0;
    dlogPrintf("log cleared from UI");
    server.send(200, "text/plain", "cleared");
    return;
  }
  String r;
  r.reserve(dlog.used + 512);
  r += "=== OTTO DEVICE LOG ===\r\n";
  r += "Boots since power-on : "; r += (unsigned long)dlog.boots; r += "\r\n";
  r += "This boot's reason   : "; r += resetReasonStr(esp_reset_reason()); r += "\r\n";
  r += "Uptime               : "; r += millis() / 1000; r += " s\r\n";
  r += "Free heap            : "; r += ESP.getFreeHeap(); r += " bytes\r\n";
  r += "Min free heap ever   : "; r += ESP.getMinFreeHeap(); r += " bytes\r\n";
  r += "Largest free block   : "; r += ESP.getMaxAllocHeap(); r += " bytes\r\n";
  r += "-------------------------------------------\r\n";
  if (dlog.used < DLOG_SIZE) {
    for (uint32_t i = 0; i < dlog.used; i++) r += dlog.buf[i];
  } else {
    for (uint32_t i = 0; i < DLOG_SIZE; i++)
      r += dlog.buf[(dlog.head + i) % DLOG_SIZE];
  }
  server.send(200, "text/plain", r);
}

// =============================================================
//  TFT — pins & object  (ESP32-S3-N16R8 pin map)
//  Active profile uses GPIO1/2/21/42/47; camera and servo pins are separate.
//  The inactive legacy map below conflicts with R8 memory.
// =============================================================
#if BOB_VISUAL_BRINGUP
#define TFT_CS   21
#define TFT_RST  47
#define TFT_DC   42
#define TFT_MOSI 1
#define TFT_CLK  2
#else
#define TFT_CS   40
#define TFT_RST  41
#define TFT_DC   42
#define TFT_MOSI 35
#define TFT_CLK  36
#endif

// ── Hardware SPI (software bit-bang fallback available via TFT_HW_SPI=0) ─────
// On the ESP32-S3 the default SPI bus is FSPI. The Adafruit driver's
// 4-argument constructor uses &SPI which maps to FSPI on S3 cores — no change
// needed in the constructor call itself. The active pins are not FSPI defaults
// and screenSetup() explicitly initializes the selected clock/data pins.
#define TFT_HW_SPI 1

#if TFT_HW_SPI
Adafruit_ST7735 tft = Adafruit_ST7735(&SPI, TFT_CS, TFT_DC, TFT_RST);
#else
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST);
#endif

// =============================================================
//  Colour palette  (RGB565)
// =============================================================
#define C_WHITE   0xFFFF
#define C_BLACK   0x0000
#define C_BG      0xFFFF   // white screen
#define C_EYE     0x0000   // black eyes
#define C_SHINE   0xFFFF   // white catchlight
#define C_MOUTH   0x0000   // black mouth
#define C_BLUSH   0xFBB0   // pink blush
#define C_HEART   0xF800   // red hearts
#define C_STAR    0xFFE0   // yellow stars
#define C_BROW    0x0000   // black brows
#define C_FIRE1   0xF800   // bright red  fire
#define C_FIRE2   0xFC00   // orange fire
#define C_FIRE3   0xFFE0   // yellow fire tip
#define C_SMOKE   0xC618   // light grey smoke
#define C_TEAR    0x001F   // blue tear
#define C_GREEN   0x07E0   // terminal green
#define C_SPARK   0xFFE0   // sparkle yellow

// Rainbow palette for dance mode (8 vivid colours, RGB565)
static const uint16_t DANCE_BG[] = {
  0xF800,  // 0 — red
  0xFC00,  // 1 — orange
  0xFFE0,  // 2 — yellow
  0x07E0,  // 3 — green
  0x07FF,  // 4 — cyan
  0x001F,  // 5 — blue
  0x781F,  // 6 — purple
  0xF81F,  // 7 — magenta
};
#define DANCE_COLORS 8
// Contrasting foreground for each bg (white or black)
static const uint16_t DANCE_FG[] = {
  0xFFFF,  // on red    → white
  0x0000,  // on orange → black
  0x0000,  // on yellow → black
  0xFFFF,  // on green  → white
  0x0000,  // on cyan   → black
  0xFFFF,  // on blue   → white
  0xFFFF,  // on purple → white
  0xFFFF,  // on magenta→ white
};

// =============================================================
//  Screen geometry  (landscape 160×128)
// =============================================================
#define SCR_W  160
#define SCR_H  128

// Eyes
#define EYE_R    20    // normal eye radius
#define EYE_LX   42   // left  eye centre X
#define EYE_RX  118   // right eye centre X
#define EYE_Y    50   // eye centre Y
#define SHINE_R   4   // catchlight radius

// Mouth area
#define MOUTH_Y  100  // baseline Y for mouths
#define MOUTH_CX  80  // horizontal centre

// =============================================================
//  Status bars — permanent top + bottom strips
//
//  TOP    : "BOB" on the left, WiFi arcs on the right (bright when
//           joined to the router, dim + red slash when not).
//  BOTTOM : message bar. Text typed in the web UI sits centred and still.
//           Anything longer than one bar-width is broken into pages that
//           swap every TICKER_PAGE_MS; then the bar falls back to the IP.
//
//  Both strips are BAR_H tall so they match, and NOTHING else may draw
//  inside them: the face is confined to FACE_TOP..FACE_BOT, and every
//  full-screen repaint calls drawBars() again on its way out.
// =============================================================
#define BAR_H      12
#define BOT_BAR_Y  (SCR_H - BAR_H)   // 116
#define FACE_TOP   BAR_H             // 12  — face may not draw above this
#define FACE_BOT   (BOT_BAR_Y - 1)   // 115 — ...nor below this
#define BAR_BG     C_WHITE           // white — bars blend into the face, no strip
#define BAR_FG     C_BLACK           // black text/icon ("BOB", IP, WiFi arcs)
#define BAR_DIM    0x4208            // dark grey — offline WiFi icon only

#define TICKER_SECS      5   // how long a ONE-page message stays up
#define TICKER_PAGE_MS 2000  // how long each page of a longer one stays up (min 2 s)
#define TICKER_MAX_PAGES 4   // 4 x 26 chars — plenty for a typed sentence

#define TICKER_TY    (BOT_BAR_Y + 3)        // top row of the 8 px text band
#define TICKER_FITS  (SCR_W - 4)            // usable width in px
#define TICKER_CHARS (TICKER_FITS / 6)      // 26 chars at 6 px/char

String        tickerMsg   = "";
String        tickerPages[TICKER_MAX_PAGES];
uint8_t       tickerCount = 0;      // pages in use; 0 = bar is idle
uint8_t       tickerPage  = 0;      // page currently on screen
unsigned long tickerUntil = 0;      // millis() at which the message stops
unsigned long nextTicker  = 0;      // millis() at which the page flips
unsigned long netScreenUntil = 0; // nonblocking network splash deadline
bool          lastWifiUp  = false;

// Break a message into pages of at most TICKER_CHARS, splitting on spaces
// so words stay whole. A word longer than a whole page is cut mid-word —
// there is nowhere else to put it.
void tickerSplit(const String& t) {
  tickerCount = 0;
  int i = 0, n = (int)t.length();
  while (i < n && tickerCount < TICKER_MAX_PAGES) {
    int end = i + TICKER_CHARS;
    if (end >= n) {
      end = n;
    } else {
      int sp = end;
      while (sp > i && t[sp] != ' ') sp--;   // back up to the last space
      if (sp > i) end = sp;                  // none found ⇒ keep the hard cut
    }
    tickerPages[tickerCount++] = t.substring(i, end);
    i = end;
    while (i < n && t[i] == ' ') i++;        // eat the separating spaces
  }
}

void drawTopBar() {
  bool up = (WiFi.status() == WL_CONNECTED);
  lastWifiUp = up;

  tft.fillRect(0, 0, SCR_W, BAR_H, BAR_BG);   // white — no visible strip

  // "ANVYXO" centred — and when the fallback hotspot is up, "ANVYXO  AP" is
  // centred as one group so the label stays visually balanced either way.
  // The WiFi icon keeps its own corner and is not part of the centring.
  tft.setTextWrap(false);
  tft.setTextSize(1);
  const int nameW  = 6 * 6;                    // "ANVYXO"
  const int apW    = apMode ? 5 * 6 : 0;       // 3-char gap + "AP"
  int       x      = (SCR_W - (nameW + apW)) / 2;

  tft.setTextColor(BAR_FG);
  tft.setCursor(x, 2);
  tft.print("ANVYXO");

  if (apMode) {                       // running the fallback hotspot
    tft.setTextColor(C_FIRE1);        // red — readable on white, still flags AP
    tft.setCursor(x + nameW + 3 * 6, 2);
    tft.print("AP");
  }

  // WiFi arcs, right-aligned. drawCircleHelper corner mask 0x3 = top half.
  uint16_t c = up ? BAR_FG : BAR_DIM;
  int cx = SCR_W - 9, cy = 9;
  tft.drawCircleHelper(cx, cy, 6, 0x3, c);
  tft.drawCircleHelper(cx, cy, 4, 0x3, c);
  tft.drawCircleHelper(cx, cy, 2, 0x3, c);
  tft.fillCircle(cx, cy, 1, c);
  if (!up) tft.drawLine(cx - 7, cy - 7, cx + 6, cy, C_FIRE1);   // "no link"
}

// Static part of the bar: background + separator. Only needed after a
// full-screen repaint — the marquee below never touches these rows.
void drawBottomBarFrame() {
  tft.fillRect(0, BOT_BAR_Y, SCR_W, BAR_H, BAR_BG);   // white — no visible strip
}

// Repaint ONLY the 8 px text band — and do it without flicker.
//
// The old version cleared the whole bar and then printed, so for one frame
// the text was gone; at 25 fps that reads as flicker. Instead the glyphs are
// drawn opaque (setTextColor takes a background), so every pixel under the
// text is overwritten exactly once, and only the strips the glyphs do NOT
// cover get filled. Nothing on screen is ever momentarily blank.
void drawTickerText() {
  String msg;

  if (millis() < tickerUntil && tickerCount) {
    msg = tickerPages[tickerPage];                    // one page, never moves
  } else {
    msg = wifiHomeOk && homeIP.length() ? homeIP :
          (ottoIP.length() ? ottoIP : String("ANVYXO")); // reachable network address
  }
  int w = (int)msg.length() * 6;                      // 6 px/char at size 1
  int x = (SCR_W - w) / 2;                            // always centred

  tft.setTextWrap(false);   // one page per line — never wrap onto the face
  tft.setTextSize(1);
  tft.setTextColor(BAR_FG, BAR_BG);   // opaque: each glyph carries its own bg
  tft.setCursor(x, TICKER_TY);
  tft.print(msg);                     // GFX clips glyphs that fall off-screen

  // Blank only the gaps either side of the text.
  int left = x < 0 ? 0 : (x > SCR_W ? SCR_W : x);
  if (left > 0) tft.fillRect(0, TICKER_TY, left, 8, BAR_BG);
  int right = x + w;
  if (right < 0) right = 0;
  if (right < SCR_W) tft.fillRect(right, TICKER_TY, SCR_W - right, 8, BAR_BG);
}

void drawBottomBar() { drawBottomBarFrame(); drawTickerText(); }
void drawBars()      { drawTopBar(); drawBottomBar(); }

// Flip to the next page when its 1.5 s are up. Called at the TOP of
// screenTick(), before any of its early returns, so a multi-page message
// keeps turning over in every expression.
void tickerTick() {
  unsigned long now = millis();

  if (now < tickerUntil && tickerCount) {
    if (tickerCount > 1 && now >= nextTicker) {    // single page just sits there
      nextTicker = now + TICKER_PAGE_MS;
      tickerPage++;
      if (tickerPage >= tickerCount) tickerPage = 0;
      drawTickerText();
    }
  } else if (tickerCount) {
    tickerCount = 0;                              // just expired —
    tickerMsg   = "";
    drawTickerText();                             // fall back to the IP
  }
}

// =============================================================
//  Expression + animation state
// =============================================================
// 0=Happy 1=Sad 2=Angry 3=Surprised 4=Sleepy 5=Love 6=Normal
uint8_t expression  = 6;   // start on Normal
uint8_t prevExpr    = 255;
bool    danceMode   = false;
bool    rpsHold     = false;   // true while a Python-driven RPS result is frozen on screen

// ── Scared mode ───────────────────────────────────────────────
// Raised the instant the robot is picked up or shaken, and dropped again when
// the reaction routine finishes. It OVERRIDES the face without touching
// `expression`, so whatever the user had selected is still there underneath and
// simply reappears when the scare is over — see the resume note in
// endLiftReaction(). Setting prevExpr = 255 on each edge is what makes the
// redraw happen exactly once per transition instead of every screen tick.
bool    scaredMode  = false;
uint8_t scaredFrame = 0;   // drives the small tremble animation
int8_t  scaredSweatOff = 0; // sweat drop slide offset, reset on each new scare

// True only while the foot servos are actively slewing (RS_FLIP). The scared
// face animation is SUSPENDED during this window: writing the display over SPI
// at the same instant the servos switch is what lets noise corrupt the ST7735's
// MADCTL/registers (symptom: the scared face rotates or goes blank-white and
// stays that way until a full re-init). Dance is immune precisely because it
// draws once and never touches the panel mid-slew — this makes scared behave the
// same. Declared here (not with reactState, which is defined far below) so it is
// in scope for screenTick() above its definition.
bool    feetFlipping = false;

// Lift-reaction phase. Declared up here (with the rest of the early scared state)
// because both handleState() and /imu report it and are defined above the lift
// reaction routine itself. The routine's constants/functions live further down.
enum ReactState : uint8_t { RS_IDLE = 0, RS_FLIP, RS_SETTLE, RS_RECAL };
ReactState reactState = RS_IDLE;

// ── "Help me" scared cry ──────────────────────────────────────
// When enabled, the robot shouts "help me" at FULL volume exactly 1 s after the
// scared face is painted. Toggle from the web UI (/helpme?on=0|1); persisted in
// NVS. scaredDrawnAt is stamped the moment startLiftReaction() draws the face;
// helpMePlayed guards it so the cry fires once per scare.
bool          helpMeEnabled = true;
unsigned long scaredDrawnAt = 0;
bool          helpMePlayed  = false;

// ── Scared-to-angry escalation ────────────────────────────────
// 4 threshold crossings within LIFT_WINDOW_MS triggers the scared face
// + leg kicks. When the legs finish the face goes directly to Angry.
#define LIFT_HIT_NEEDED   4
#define LIFT_WINDOW_MS 2000
static uint8_t       liftHitCount    = 0;
static unsigned long liftWindowStart = 0;

// ── Listening mode ────────────────────────────────────────────
// Whenever the mic is live the face is replaced by a big ear with sound
// waves pulsing into it. Kept alive by a deadline rather than an on/off
// flag: /miclevel is polled ~2.5x a second while the Mic Meter is ON, and
// each poll pushes the deadline out, so the ear simply fades away ~1.2 s
// after the meter is switched off. Recording sets a deadline that covers
// the whole capture.
unsigned long listenUntil = 0;
uint8_t       listenFrame = 0;
bool          prevListen  = false;
bool listenActive() { return millis() < listenUntil; }

// ── Idle animation ────────────────────────────────────────────
uint8_t bobState   = 0;
uint8_t bobCounter = 0;
int8_t  gazeOX     = 0;
bool    winkL      = false;
bool    winkR      = false;

// ── Per-expression animation frame counter ────────────────────
uint8_t animFrame  = 0;   // increments every 80ms tick

// Dance
uint8_t danceFrame = 0;

// ── Animated particle state ───────────────────────────────────
// Tears (sad): 4 drops, each has a Y position
int8_t  tearY[4]    = {0, -5, -12, -18};  // stagger start
bool    tearActive  = false;

// Fire (angry): 8 particles per eye
struct Particle { int8_t x, y; uint8_t life; };
Particle fireL[6], fireR[6];
bool     fireInit  = false;

// Smoke (angry): 3 puffs from nose
struct Puff { int8_t x, y; uint8_t r, life; };
Puff smoke[3];

// Zzz (sleepy): 3 letters floating up
// int16_t, not int8_t: the screen is 160 px wide and these spawn near the
// right edge — an int8_t x wraps negative past 127 and the Z never appears.
struct Zzz { int16_t x, y; uint8_t life; uint8_t sz; };
Zzz zzzBubbles[3];
bool zzzInit = false;

// Surprised brow bounce
int8_t browOffset = 0;   // pixel offset added to brow Y (neg = up)
int8_t browDir    = -1;

// Sparkle (happy) — int16_t x: spawns anywhere across the 160 px width
struct Spark { int16_t x, y; uint8_t life; };
Spark sparks[5];
bool sparkInit = false;

// Floating hearts (love) — int16_t x: spawns anywhere across the 160 px width
struct Heart { int16_t x, y; uint8_t life; uint8_t sz; };
Heart hearts[4];
bool heartFloatInit = false;

// Yawn (sleepy): mouth open amount 0-16
uint8_t yawnOpen = 0;
int8_t  yawnDir  = 1;

// ── Blink timing ──────────────────────────────────────────────
#define BLINK_STEPS  7
#define BLINK_MS     13
#define BLINK_MIN  3000
#define BLINK_MAX  6000
unsigned long nextBlink = 0;
unsigned long nextAnim  = 0;

// =============================================================
//  I2S — MAX98357A speaker + SPH0645 microphone
// =============================================================

// ── Speaker (MAX98357A) — dedicated test map ────────────────
#define I2S_SPK_NUM    I2S_NUM_0
#define I2S_SPK_BCLK   3   // strapping pin; amp input must not disturb reset strap
#define I2S_SPK_LRC    45  // strapping pin; amp input must not disturb reset strap
#define I2S_SPK_DIN    14   // existing ESP32-S3 -> MAX98357A data connection

// ── Microphone (SPH0645) — I2S port 1 ────────────────────────
#define I2S_MIC_NUM    I2S_NUM_1
#if BOB_MIC_TEST
#define I2S_MIC_SCK    43   // mic BCLK; USB CDC keeps UART0 logs on USB
#define I2S_MIC_WS     44   // mic LRCL/WS
#define I2S_MIC_SD     48   // mic DOUT; GPIO48 also feeds onboard RGB LED input
#else
#define I2S_MIC_SCK    33
#define I2S_MIC_WS     34
#define I2S_MIC_SD     21
#endif

#define SAMPLE_RATE          16000
#define RECORD_SECS          6    // max clip length; actual alloc now uses flash not RAM
#define DEFAULT_SILENCE_MS   2000
#define MIN_VOICE_RMS        250
#define RAM_RECORD_SECS      3
#define WAV_BUF_BYTES        (SAMPLE_RATE * RAM_RECORD_SECS * 2)   // 16-bit mono quick capture

static int16_t* wavBuf      = nullptr;   // heap-allocated on first record
static size_t   wavBufLen   = 0;         // bytes actually captured
bool            isRecording = false;

// ── Runtime-adjustable audio config (set from the UI, saved to NVS) ──
// The mic driver can be re-installed live with any combination of these,
// so wrong-channel / wrong-format / clone-mic problems can be ruled out
// from the browser without reflashing.
uint8_t micCh    = 0;    // 0 = LEFT slot (SEL->GND), 1 = RIGHT slot
uint8_t micFmt   = 0;    // 0 = standard I2S, 1 = MSB (left-justified)
uint8_t micFix   = 1;    // SPH0645 timing-fix registers on/off
uint8_t micShift = 14;   // raw 32-bit >> shift (SPH0645: 14, INMP441: 16, louder: 11)
uint8_t spkVol   = 100;  // 5..100 — beep amplitude and SAM voice gain
bool    micInstalled = false;

// Install I2S driver for the MAX98357A speaker (output)
// Use RIGHT_LEFT (stereo) so every DMA slot is filled — ONLY_LEFT sends silence
// on every other slot, halving effective output power on the MAX98357A.
void i2s_speaker_init() {
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,   // stereo — fill both slots
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 8,
    .dma_buf_len          = 64,
    .use_apll             = false,
    .tx_desc_auto_clear   = true,
    .fixed_mclk           = 0
  };
  i2s_pin_config_t pins = {
    // mck MUST be NO_CHANGE: if left out it defaults to GPIO0, and the two
    // I2S ports then fight over it — the second i2s_set_pin fails with 258.
    .mck_io_num   = I2S_PIN_NO_CHANGE,
    .bck_io_num   = I2S_SPK_BCLK,
    .ws_io_num    = I2S_SPK_LRC,
    .data_out_num = I2S_SPK_DIN,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };
  esp_err_t e1 = i2s_driver_install(I2S_SPK_NUM, &cfg, 0, nullptr);
  esp_err_t e2 = i2s_set_pin(I2S_SPK_NUM, &pins);
  i2s_zero_dma_buffer(I2S_SPK_NUM);
  dlogPrintf("i2s speaker: install=%d setpin=%d (0=OK)", (int)e1, (int)e2);
  if (e1 == ESP_OK && e2 == ESP_OK)
    hwSet(HW_SPEAKER, HW_NOCHECK, "I2S ok on GPIO %d/%d/%d (amp can't answer back)",
          I2S_SPK_BCLK, I2S_SPK_LRC, I2S_SPK_DIN);
  else
    hwSet(HW_SPEAKER, HW_FAIL, "I2S driver error install=%d pin=%d", (int)e1, (int)e2);
}

// Install (or re-install) the mic I2S driver using the current micCh /
// micFmt / micFix settings. Safe to call repeatedly — used by the UI's
// mic config scanner.
void i2s_mic_init() {
  if (micInstalled) { i2s_driver_uninstall(I2S_MIC_NUM); micInstalled = false; }
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,  // mic outputs 32-bit frames
    .channel_format       = micCh ? I2S_CHANNEL_FMT_ONLY_RIGHT : I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = micFmt ? I2S_COMM_FORMAT_STAND_MSB : I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 8,
    .dma_buf_len          = 64,
    .use_apll             = false,
    .tx_desc_auto_clear   = false,
    .fixed_mclk           = 0
  };
  i2s_pin_config_t pins = {
    .mck_io_num   = I2S_PIN_NO_CHANGE,   // see note in i2s_speaker_init
    .bck_io_num   = I2S_MIC_SCK,
    .ws_io_num    = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_MIC_SD
  };
  esp_err_t e1 = i2s_driver_install(I2S_MIC_NUM, &cfg, 0, nullptr);
  esp_err_t e2 = i2s_set_pin(I2S_MIC_NUM, &pins);
  dlogPrintf("i2s mic: install=%d setpin=%d ch=%d fmt=%d fix=%d shift=%d",
             (int)e1, (int)e2, micCh, micFmt, micFix, micShift);
// SPH0645 timing-fix register writes removed — those registers are
// classic-ESP32 only and do not exist on the ESP32-S3. If the mic reads
// garbage on S3, use the micFmt=1 (MSB) setting from the UI instead.
  micInstalled = (e1 == ESP_OK && e2 == ESP_OK);
  if (micInstalled) {
    hwSet(HW_MIC, HW_NOCHECK, "I2S RX on GPIO %d/%d/%d; signal checked by meter",
          I2S_MIC_SCK, I2S_MIC_WS, I2S_MIC_SD);
  } else {
    if (e1 == ESP_OK) i2s_driver_uninstall(I2S_MIC_NUM);
    hwSet(HW_MIC, HW_FAIL, "I2S init failed install=%d setpin=%d", (int)e1, (int)e2);
  }
}

// =============================================================
//  Audio diagnostics — GET /diag
//  Returns plain-text report: copy/paste for debugging.
// =============================================================
void handleDiag() {
  dlogPrintf("diag: start (heap=%u)", ESP.getFreeHeap());
  String r = "";

  // ── 1. Heap ───────────────────────────────────────────────
  r += "=== OTTO AUDIO DIAGNOSTIC ===\r\n";
  r += "Free heap : "; r += (int)ESP.getFreeHeap(); r += " bytes\r\n";
  r += "Audio buf : flash (/_rec_tmp.wav)\r\n";
  r += "\r\n";

  // ── 2. Mic raw sample dump ────────────────────────────────
  r += "--- MIC (SPH0645) ---\r\n";
  static int32_t diagRaw[16];
  size_t br = 0;
  i2s_zero_dma_buffer(I2S_MIC_NUM);
  delay(10);
  esp_err_t err = i2s_read(I2S_MIC_NUM, diagRaw, sizeof(diagRaw), &br, pdMS_TO_TICKS(300));
  r += "i2s_read  : "; r += (err == ESP_OK ? "ESP_OK" : "FAILED"); r += "\r\n";
  r += "bytes read: "; r += (int)br; r += "\r\n";

  int32_t mn = diagRaw[0], mx = diagRaw[0];
  for (int i = 0; i < 16; i++) { if(diagRaw[i]<mn) mn=diagRaw[i]; if(diagRaw[i]>mx) mx=diagRaw[i]; }
  r += "raw min   : "; r += mn; r += "\r\n";
  r += "raw max   : "; r += mx; r += "\r\n";
  for (int i = 0; i < 8; i++) {
    char buf[40];
    snprintf(buf, sizeof(buf), "raw[%d]    : 0x%08X  (%d)\r\n", i, (unsigned)diagRaw[i], diagRaw[i]);
    r += buf;
  }
  if (mn == 0 && mx == 0)
    r += "RESULT    : ALL ZEROS -> wiring fault (check SD pin, SEL->GND, VDD->3.3V)\r\n";
  else if (mn == mx)
    r += "RESULT    : CONSTANT NON-ZERO -> stuck signal, check SEL pin\r\n";
  else
    r += "RESULT    : SIGNAL PRESENT -> mic wiring OK\r\n";
  r += "\r\n";

  // ── 3. Speaker 1-second test tone ────────────────────────
  r += "--- SPEAKER (MAX98357A) ---\r\n";
  r += "Playing 1000 Hz tone for 1 second...\r\n";
  int16_t tbuf[128];
  int written_total = 0;
  for (int s = 0; s < SAMPLE_RATE; ) {
    int mono = min(64, SAMPLE_RATE - s);
    for (int k = 0; k < mono; k++) {
      float t = (float)(s+k) / SAMPLE_RATE;
      int16_t v = (int16_t)(28000.0f * sinf(2.0f * 3.14159f * 1000.0f * t));
      tbuf[k*2]   = v;
      tbuf[k*2+1] = v;
    }
    size_t wr = 0;
    i2s_write(I2S_SPK_NUM, tbuf, mono*4, &wr, pdMS_TO_TICKS(200));
    written_total += (int)wr;
    s += mono;
  }
  i2s_zero_dma_buffer(I2S_SPK_NUM);
  r += "bytes sent: "; r += written_total; r += "\r\n";
  if (written_total > 0)
    r += "RESULT    : i2s_write OK -> if silent check VIN->5V, speaker on +/-, SD_MODE->3.3V\r\n";
  else
    r += "RESULT    : i2s_write wrote 0 -> speaker I2S driver issue\r\n";

  server.send(200, "text/plain", r);
}

// Play a sine tone on the speaker (stereo pairs — port is RIGHT_LEFT)
void playTone(int freqHz, int durMs) {
  int total = (SAMPLE_RATE * durMs) / 1000;
  int16_t chunk[128];   // 64 stereo pairs
  for (int s = 0; s < total; ) {
    int mono = min(64, total - s);
    for (int k = 0; k < mono; k++) {
      float t   = (float)(s + k) / SAMPLE_RATE;
      int16_t v = (int16_t)(28000.0f * sinf(2.0f * 3.14159f * freqHz * t));
      chunk[k*2]   = v;
      chunk[k*2+1] = v;
    }
    size_t wr = 0;
    i2s_write(I2S_SPK_NUM, chunk, mono * 4, &wr, pdMS_TO_TICKS(500));
    s += mono;
    yield();
  }
  i2s_zero_dma_buffer(I2S_SPK_NUM);
}

// GET /testtone?f=1000&ms=800 — speaker-only test (no mic involved)
void handleTestTone() {
  int f  = server.hasArg("f")  ? constrain(server.arg("f").toInt(),  100, 4000) : 1000;
  int ms = server.hasArg("ms") ? constrain(server.arg("ms").toInt(), 100, 3000) : 800;
  server.send(200, "text/plain", "ok");
  dlogPrintf("testtone: %d Hz %d ms (heap=%u)", f, ms, ESP.getFreeHeap());
  playTone(f, ms);
}

// GET /miclevel — grab ~100 ms of mic audio, return JSON level stats.
// Polled by the UI level meter: peak==0 means the mic line is dead.
void handleMicLevel() {
  // The UI polls this every 400 ms while the Mic Meter is ON, so refreshing
  // the deadline here is all it takes to keep the listening ear on screen —
  // and it clears itself ~1.2 s after the meter is switched off.
  listenUntil = millis() + 1200;

  static int32_t raw[256];
  int32_t mn = 0, mx = 0;
  int64_t sum = 0, sumsq = 0;
  size_t  count = 0;
  bool    first = true;
  unsigned long t0 = millis();

  while (count < 1600 && millis() - t0 < 400) {   // ~100 ms @ 16 kHz
    size_t br = 0;
    if (i2s_read(I2S_MIC_NUM, raw, sizeof(raw), &br, pdMS_TO_TICKS(100)) != ESP_OK) break;
    size_t got = br / sizeof(int32_t);
    if (!got) break;
    for (size_t i = 0; i < got; i++) {
      int32_t s = raw[i] >> micShift;   // same shift as recordMic()
      if (first) { mn = mx = s; first = false; }
      if (s < mn) mn = s;
      if (s > mx) mx = s;
      sum   += s;
      sumsq += (int64_t)s * s;
      count++;
    }
    yield();
  }

  long rms = 0;
  if (count > 0) {
    int64_t mean = sum / (int64_t)count;
    int64_t var  = sumsq / (int64_t)count - mean * mean;
    if (var < 0) var = 0;
    rms = (long)sqrtf((float)var);   // RMS after DC removal
  }
  long peak = max(labs((long)mn), labs((long)mx));

  String j = "{\"n\":";  j += (int)count;
  j += ",\"min\":";      j += mn;
  j += ",\"max\":";      j += mx;
  j += ",\"peak\":";     j += peak;
  j += ",\"rms\":";      j += rms;
  j += "}";
  server.send(200, "application/json", j);
}


// Record `secs` (1..RECORD_SECS) of audio from SPH0645 into wavBuf.
// If `stopOnSilence` is true, end early after `silenceMs` of quiet audio.
// SPH0645 sends 24-bit data in the MSBs of a 32-bit frame; we shift down to 16-bit.
void recordMic(int secs, bool stopOnSilence = false, int silenceMs = DEFAULT_SILENCE_MS) {
  secs = constrain(secs, 1, RAM_RECORD_SECS);
  silenceMs = constrain(silenceMs, 250, secs * 1000);
  if (!wavBuf) {
    wavBuf = (int16_t*)malloc(WAV_BUF_BYTES);
    if (!wavBuf) { Serial.println("recordMic: out of memory"); return; }
  }
  wavBufLen   = 0;
  isRecording = true;

  static int32_t raw[256];            // read 256 frames at a time, not 1
  const size_t need = (size_t)SAMPLE_RATE * secs;   // 16-bit samples wanted
  size_t total = 0;
  size_t quietSamples = 0;
  const size_t silenceSamplesNeeded = ((size_t)SAMPLE_RATE * (size_t)silenceMs) / 1000;

  i2s_zero_dma_buffer(I2S_MIC_NUM);

  // Discard ~50 ms: the SPH0645 outputs junk while its clock settles
  size_t junk = SAMPLE_RATE / 20;
  while (junk > 0) {
    size_t br = 0;
    if (i2s_read(I2S_MIC_NUM, raw, sizeof(raw), &br, pdMS_TO_TICKS(200)) != ESP_OK) break;
    size_t got = br / sizeof(int32_t);
    if (!got) break;
    junk = (got >= junk) ? 0 : junk - got;
  }

  unsigned long tStart = millis();
  while (total < need) {
    size_t br = 0;
    if (i2s_read(I2S_MIC_NUM, raw, sizeof(raw), &br, pdMS_TO_TICKS(200)) != ESP_OK) break;
    size_t got = br / sizeof(int32_t);
    if (!got) break;

    int64_t sumsq = 0;
    for (size_t i = 0; i < got && total < need; i++) {
      // SPH0645: 18-bit audio left-justified in bits[31:14] -> shift 14.
      // INMP441: 24-bit in bits[31:8] -> shift 16 fits int16_t.
      // micShift is adjustable from the UI's Mic Fix panel.
      int32_t s32 = raw[i] >> micShift;
      int16_t s16 = (int16_t)constrain(s32, -32768, 32767);
      wavBuf[total++] = s16;
      sumsq += (int32_t)s16 * (int32_t)s16;
    }

    if (stopOnSilence) {
      long rms = got ? (long)sqrtf((float)(sumsq / (int64_t)got)) : 0;
      if (rms < MIN_VOICE_RMS) quietSamples += got;
      else quietSamples = 0;
      if (quietSamples >= silenceSamplesNeeded) break;
    }

    yield();   // let WiFi/idle tasks run so the watchdog stays happy
    if (millis() - tStart > (unsigned long)(secs + 2) * 1000UL) break;  // hard timeout
  }

  // Strip the mic's DC offset so the WAV isn't sitting off-centre
  if (total > 0) {
    int64_t sum = 0;
    for (size_t i = 0; i < total; i++) sum += wavBuf[i];
    int32_t dc = (int32_t)(sum / (int64_t)total);
    for (size_t i = 0; i < total; i++) {
      int32_t v = (int32_t)wavBuf[i] - dc;
      wavBuf[i] = (int16_t)constrain(v, -32768, 32767);
    }
  }

  wavBufLen   = total * 2;   // bytes
  isRecording = false;
  Serial.printf("recordMic: captured %u bytes\n", (unsigned)wavBufLen);
}

// ── Buzzer-style TTS: sine tone burst per character ───────────────────────────
// Vowels → higher pitch (700 Hz), consonants → lower pitch (450 Hz).
// Samples are written as stereo pairs (L+R same value) because the speaker
// I2S port is now configured RIGHT_LEFT — each i2s_write call must supply
// 2 × samples or the DMA alternates signal/silence and the output is weak.
void speakText(const String& text) {
  const int    LONG_MS  = 150;   // consonant duration
  const int    SHORT_MS =  90;   // vowel / space duration
  const int    GAP_MS   =  40;   // silence between chars
  const float  AMP      = 280.0f * spkVol;  // vol 100 -> 28000, near full-scale

  for (int ci = 0; ci < (int)text.length(); ci++) {
    char c = tolower(text[ci]);
    bool isVowel = (c=='a'||c=='e'||c=='i'||c=='o'||c=='u'||c==' ');
    int  freqHz  = isVowel ? 700 : 450;   // vowels higher, consonants lower
    int  durMs   = isVowel ? SHORT_MS : LONG_MS;
    int  numSamples = (SAMPLE_RATE * durMs) / 1000;

    // Write stereo pairs: chunk[2k] = left, chunk[2k+1] = right (same value)
    for (int s = 0; s < numSamples; ) {
      int16_t chunk[128];   // 64 stereo pairs
      int     mono = min(64, numSamples - s);
      for (int k = 0; k < mono; k++) {
        float t   = (float)(s + k) / SAMPLE_RATE;
        int16_t v = (int16_t)(AMP * sinf(2.0f * 3.14159f * freqHz * t));
        chunk[k*2]   = v;   // left
        chunk[k*2+1] = v;   // right
      }
      size_t written;
      i2s_write(I2S_SPK_NUM, chunk, mono * 4, &written, portMAX_DELAY);
      s += mono;
    }

    // Silent gap between characters (stereo zeros)
    int     gapSamples = (SAMPLE_RATE * GAP_MS) / 1000;
    int16_t silence[64] = {0};  // 32 stereo pairs of zeros
    for (int g = 0; g < gapSamples; g += 32) {
      int sz = min(32, gapSamples - g);
      size_t written;
      i2s_write(I2S_SPK_NUM, silence, sz * 4, &written, portMAX_DELAY);
    }
    yield();
  }
  i2s_zero_dma_buffer(I2S_SPK_NUM);
}

void playWavFile(const String& path);  // defined in the LittleFS section below

// Play a WAV file from LittleFS with a hard gain multiplier (ignores spkVol).
// gain=400 means 4× — samples are clamped to ±32767 so there is no wrap-around
// distortion, just soft saturation at the loudest peaks.
void playWavFileMaxVol(const String& path, int gainPct = 400) {
  File f = LittleFS.open(path, "r");
  if (!f) { dlogPrintf("playMaxVol: %s not found", path.c_str()); return; }
  uint8_t hdr[44];
  if (f.read(hdr, 44) != 44) { f.close(); return; }
  uint32_t rate  = (uint32_t)hdr[24] | ((uint32_t)hdr[25]<<8) | ((uint32_t)hdr[26]<<16) | ((uint32_t)hdr[27]<<24);
  uint16_t chans = (uint16_t)hdr[22] | ((uint16_t)hdr[23]<<8);
  if (rate < 4000 || rate > 48000) rate = SAMPLE_RATE;
  if (chans != 1 && chans != 2)    chans = 1;
  dlogPrintf("playMaxVol: %s rate=%u ch=%u gain=%d%%", path.c_str(), (unsigned)rate, (unsigned)chans, gainPct);

  i2s_set_sample_rates(I2S_SPK_NUM, rate);
  int16_t in[64], chunk[128];
  while (true) {
    int n = f.read((uint8_t*)in, sizeof(in));
    if (n <= 0) break;
    int samples = n / 2;
    size_t wr = 0;
    for (int k = 0; k < samples; k++) {
      int32_t v = (int32_t)in[k] * gainPct / 100;
      if (v >  32767) v =  32767;
      if (v < -32768) v = -32768;
      chunk[k*2] = (int16_t)v; chunk[k*2+1] = (int16_t)v;   // mono→stereo
    }
    i2s_write(I2S_SPK_NUM, chunk, samples * 4, &wr, pdMS_TO_TICKS(500));
    yield();
  }
  f.close();
  i2s_zero_dma_buffer(I2S_SPK_NUM);
  i2s_set_sample_rates(I2S_SPK_NUM, SAMPLE_RATE);
}

// Play HelpmeSound.wav at maximum amplified volume (4× gain, clamped).
// Called from loop() 1 s after the scared face appears.
void playHelpMe() {
  playWavFileMaxVol("/HelpmeSound.wav", 400);   // 4× gain — as loud as the amp can go
}

// Build a minimal PCM WAV header (44 bytes) in-place at the front of a buffer
void buildWavHeader(uint8_t* hdr, uint32_t dataBytes) {
  uint32_t fileSize  = dataBytes + 36;
  uint32_t byteRate  = SAMPLE_RATE * 2;  // 16-bit mono
  memcpy(hdr,      "RIFF", 4);
  memcpy(hdr +  8, "WAVE", 4);
  memcpy(hdr + 12, "fmt ", 4);
  hdr[ 4] = (fileSize)      & 0xFF;
  hdr[ 5] = (fileSize >> 8) & 0xFF;
  hdr[ 6] = (fileSize >>16) & 0xFF;
  hdr[ 7] = (fileSize >>24) & 0xFF;
  hdr[16] = 16; hdr[17]=0; hdr[18]=0; hdr[19]=0;  // PCM chunk size
  hdr[20] = 1;  hdr[21]=0;                          // PCM format
  hdr[22] = 1;  hdr[23]=0;                          // mono
  hdr[24] = (SAMPLE_RATE)      & 0xFF;
  hdr[25] = (SAMPLE_RATE >> 8) & 0xFF;
  hdr[26] = (SAMPLE_RATE >>16) & 0xFF;
  hdr[27] = (SAMPLE_RATE >>24) & 0xFF;
  hdr[28] = (byteRate)      & 0xFF;
  hdr[29] = (byteRate >> 8) & 0xFF;
  hdr[30] = (byteRate >>16) & 0xFF;
  hdr[31] = (byteRate >>24) & 0xFF;
  hdr[32] = 2;  hdr[33]=0;   // block align
  hdr[34] = 16; hdr[35]=0;   // bits per sample
  memcpy(hdr + 36, "data", 4);
  hdr[40] = (dataBytes)      & 0xFF;
  hdr[41] = (dataBytes >> 8) & 0xFF;
  hdr[42] = (dataBytes >>16) & 0xFF;
  hdr[43] = (dataBytes >>24) & 0xFF;
}

// (WiFi AP constants and `server` are declared near the top of the file,
//  above the I2S helpers that use them.)

// =============================================================
//  Direct Servo GPIO (no PCA9685)
// =============================================================
// ESP32-S3-N16R8: GPIOs 4-18 reserved by CSI camera.
// GPIO19/20 = USB. GPIO43/44 = UART. GPIO0/3/45/46 = strapping.
// WARNING: GPIO37 is reserved on R8 modules; this legacy map needs board verification.
#if BOB_VISUAL_BRINGUP
#define SERVO_PIN_LH  40
#define SERVO_PIN_LF  38
#define SERVO_PIN_RH  39
#define SERVO_PIN_RF  41
// Conservative initial travel; test with horns/linkages unloaded.
#define SERVO_MIN_US 1000
#define SERVO_MAX_US 2000
#else
#define SERVO_PIN_LH  37   // Legacy map: conflicts with R8 memory
#define SERVO_PIN_LF  38
#define SERVO_PIN_RH  39
#define SERVO_PIN_RF  48
#define SERVO_MIN_US 700
#define SERVO_MAX_US 2300
#endif

#if BOB_VISUAL_BRINGUP
#include "validate_visual_pins.h"
#endif

#define CH_LH  0
#define CH_LF  1
#define CH_RH  2
#define CH_RF  3

// 50 Hz servo PWM straight from the ESP32 core's LEDC API. ESP32Servo 3.2.1
// hangs forever inside attach() on core 3.3.x (ESP32-S3), and a pulse width
// is all a hobby servo needs. Channels 0-3 sit on LEDC timers 0-1, leaving
// timer 3 / channel 7 free for the camera's XCLK.
#define SERVO_LEDC_BITS 14
struct ServoPwm {
  int pin = -1;
  bool attach(int p, uint8_t ch) {
    if (!ledcAttachChannel(p, 50, SERVO_LEDC_BITS, ch)) return false;
    pin = p;
    return true;
  }
  bool writeMicroseconds(int us) {
    if (pin < 0) return false;
    us = constrain(us, SERVO_MIN_US, SERVO_MAX_US);
    return ledcWrite(pin, (uint32_t)us * ((1u << SERVO_LEDC_BITS) - 1) / 20000u);
  }
};
static ServoPwm servos[4];

// =============================================================
//  Trim + motion params
// =============================================================
int trim[4]    = {0, 0, 0, 0};
int stepPeriod = 600;
int hipSwing   = 25;
int footTilt   = 20;

Preferences prefs;
#include "servo_mapping.h"
int servoPins[4] = {SERVO_PIN_LH, SERVO_PIN_LF, SERVO_PIN_RH, SERVO_PIN_RF};
bool servoMapSaved = false;

bool persistServoLayout(const int* pins, const int* trims) {
  bobservo::Layout layout = bobservo::makeLayout(pins, trims);
  if (!bobservo::valid(layout)) return false;
  Preferences storage;
  if (!storage.begin("bobservo", false)) return false;
  bool ok = storage.putBytes("layout", &layout, sizeof(layout)) == sizeof(layout);
  storage.end();
  return ok;
}
void loadTrims() {
  prefs.begin("otto", true); // migrate trims from firmware before editable mapping
  trim[0] = constrain(prefs.getInt("t0", 0), -45, 45);
  trim[1] = constrain(prefs.getInt("t1", 0), -45, 45);
  trim[2] = constrain(prefs.getInt("t2", 0), -45, 45);
  trim[3] = constrain(prefs.getInt("t3", 0), -45, 45);
  prefs.end();
  Preferences storage;
  if (!storage.begin("bobservo", true)) return;
  bobservo::Layout layout{};
  const bool ok = storage.getBytesLength("layout") == sizeof(layout) &&
    storage.getBytes("layout", &layout, sizeof(layout)) == sizeof(layout) && bobservo::valid(layout);
  storage.end();
  if (ok) {
    for (int i=0;i<4;i++) { servoPins[i]=layout.pins[i]; trim[i]=layout.trims[i]; }
    servoMapSaved=true;
  } else dlogPrintf("servo: invalid saved layout; using default mapping and legacy trims");
}
bool saveTrims() {
  bool ok = persistServoLayout(servoPins, trim);
  if (ok) servoMapSaved=true;
  return ok;
}

void loadMicCfg() {
  prefs.begin("otto", true);
  micCh    = prefs.getUChar("mch", 0);
  micFmt   = prefs.getUChar("mfmt", 0);
  micFix   = prefs.getUChar("mfix", 1);
  micShift = prefs.getUChar("msh", 14);
  prefs.end();
}
void saveMicCfg() {
  prefs.begin("otto", false);
  prefs.putUChar("mch", micCh);
  prefs.putUChar("mfmt", micFmt);
  prefs.putUChar("mfix", micFix);
  prefs.putUChar("msh", micShift);
  prefs.end();
}

// =============================================================
//  Servo helpers
// =============================================================
int degToUs(int deg) {
  return map(constrain(deg, 0, 180), 0, 180, SERVO_MIN_US, SERVO_MAX_US);
}
// Last angle commanded on each channel, WITHOUT trim, so glides can work out
// where they are starting from. Every write path below keeps it up to date —
// if one of them didn't, the next glide would interpolate from a stale
// position and jump.
int servoCur[4] = {90, 90, 90, 90};
static bobmotion::Engine motion;
void motionDisarm(const char* why);
void motionTick();
void motionLoadSettings();

// The legacy tilt moves above the IMU implementation use these live values.
extern int16_t imu_ax, imu_ay, imu_az, imu_gz, imuCalGz;
extern bool imuCalValid;
extern float imuCalPitch, imuCalRoll;
static void accelPitchRoll(int16_t ax, int16_t ay, int16_t az, float* pitch, float* roll);
static float angleDelta(float a, float b);

void setServo(uint8_t ch, int deg) {
  int us = degToUs(constrain(deg + trim[ch], 0, 180));
  servos[ch].writeMicroseconds(us);
  servoCur[ch] = deg;
}
void setServoRaw90() {
  int us = degToUs(90);
  for (int i = 0; i < 4; i++) { servos[i].writeMicroseconds(us); servoCur[i] = 90-trim[i]; }
}
// The single source of truth for "where a leg rests". Every gait, dance and
// IMU move builds its offsets around this instead of a hardcoded 90°. When a
// calibrated idle pose has been saved (Save Calibrated Position / Motion Studio
// capture) it is that pose; otherwise it falls back to the raw servo centre.
// This is why the old forward/back/dance looked wrong: they swung around 90°
// while the robot actually stands at ~[80,65,90,45], so every motion fought the
// assembly. Centring on homeAngle() fixes that in one place.
int homeAngle(uint8_t ch) {
  return motion.config.customNeutral ? (int)lroundf(motion.config.neutral[ch]) : 90;
}
// Move one channel to home + delta (used by the stepped walking gaits).
void setServoOffset(uint8_t ch, int delta) { setServo(ch, homeAngle(ch) + delta); }
void standNeutral() {
  for (unsigned i=0;i<4;++i) setServo(i, homeAngle(i));
}

// ── Smooth servo glide ─────────────────────────────────────────
// Interpolates all 4 channels from their current angles to the targets over
// `ms` milliseconds, one micro-step per PWM frame.

void setServoSmooth(uint8_t ch, int deg) {
  if (deg == servoCur[ch]) return;   // nothing changed — don't touch the PWM
  int us = degToUs(constrain(deg + trim[ch], 0, 180));
  servos[ch].writeMicroseconds(us);
  servoCur[ch] = deg;  // store without trim for easy math
}

// One servo PWM frame at 50 Hz. Writing a servo more often than this is
// pointless AND harmful: the ESP32's LEDC hardware only latches a new pulse
// width once per 20 ms frame, so extra writes are either dropped or land at a
// random phase within the frame. The old glide stepped every ~6 ms (140 ms /
// 16 + 4 channels), so two out of every three commanded positions vanished and
// the survivors arrived at uneven intervals — that aliasing is exactly what
// dance jitter looks like. It did not show up with the PCA9685 because each
// I2C register write took ~1 ms and accidentally rate-limited the whole glide.
// Driving the GPIOs directly removed that brake, so we now enforce it here.
#define SERVO_FRAME_MS 20

// Move all 4 servos to targets simultaneously over `ms` ms, one micro-step per
// PWM frame, with a cosine ease-in/ease-out so each move starts and ends at
// zero velocity. The easing matters as much as the frame lock: back-to-back
// linear glides reverse direction instantly at the beat boundary, which the
// servo answers with a current spike and an audible snap.
// Yields between steps (never re-enters the web server).
void glideTo(int lh, int lf, int rh, int rf, int ms) {
  int from[4] = {servoCur[0], servoCur[1], servoCur[2], servoCur[3]};
  int to[4]   = {lh, lf, rh, rf};
  int n = ms / SERVO_FRAME_MS;
  if (n < 1) n = 1;
  for (int s = 1; s <= n; s++) {
    float e = 0.5f - 0.5f * cosf(PI * (float)s / (float)n);   // 0 -> 1, eased
    for (int ch = 0; ch < 4; ch++) {
      int pos = from[ch] + (int)lroundf((to[ch] - from[ch]) * e);
      setServoSmooth(ch, pos);
    }
    delay(SERVO_FRAME_MS);
    yield();   // NOT server.handleClient() - see notes
  }
}

void glideSafe(int lh, int lf, int rh, int rf, int ms) {
  glideTo(lh, lf, rh, rf, ms);
}

void glideNeutral(int ms = 240) {
  glideTo(homeAngle(0), homeAngle(1), homeAngle(2), homeAngle(3), ms);
}

// Glide all four channels to the calibrated home pose plus the given per-joint
// offsets, eased. This is the home-relative primitive every dance uses so a
// single Save Calibrated Position re-centres the whole expression library.
void glideOffset(int dlh, int dlf, int drh, int drf, int ms) {
  glideTo(homeAngle(0)+dlh, homeAngle(1)+dlf, homeAngle(2)+drh, homeAngle(3)+drf, ms);
}

// =============================================================
//  Gaits
// =============================================================
// Smooth glided gaits. On a heavy tabletop bot these read as a rocking waddle
// (forward/back) and a hip-twist pivot (left/right) rather than true travel, but
// they are fluid and coordinated instead of the old buzzy per-joint steps. The
// dashboard sliders still drive them: hipSwing/footTilt = amplitude, stepPeriod
// = tempo. Every offset is home-relative and cosine-eased via glideOffset.
// Step-by-step gaits: each step is lift-foot -> swing/rock -> plant -> a short
// pause, so it reads as distinct marching rather than a continuous sway. Every
// offset is home-relative and eased. hipSwing/footTilt set amplitude, stepPeriod
// tempo (dashboard sliders). A heavy tabletop bot marches in place; the discrete
// lift/plant/pause is what sells "walking".
void walkForward(int steps) {
  int H = hipSwing, F = footTilt, t = stepPeriod / 3;
  for (int s = 0; s < steps; s++) {
    glideOffset(+H, +F, +H,  0, t);  delay(60);    // rock left, LEFT foot lifts
    glideOffset(+H,  0, +H,  0, t);  delay(150);    // plant left — a clear step
    glideOffset(-H,  0, -H, +F, t);  delay(60);     // rock right, RIGHT foot lifts
    glideOffset(-H,  0, -H,  0, t);  delay(150);     // plant right
    yield();
  }
  glideNeutral();
}
void walkBackward(int steps) {
  int H = hipSwing, F = footTilt, t = stepPeriod / 3;
  for (int s = 0; s < steps; s++) {
    glideOffset(-H,  0, -H, +F, t);  delay(60);     // step back leads with the trailing foot
    glideOffset(-H,  0, -H,  0, t);  delay(150);
    glideOffset(+H, +F, +H,  0, t);  delay(60);
    glideOffset(+H,  0, +H,  0, t);  delay(150);
    yield();
  }
  glideNeutral();
}
void turnLeft(int steps) {
  int H = hipSwing, F = footTilt, t = stepPeriod / 3;
  for (int s = 0; s < steps; s++) {
    glideOffset(-H, +F, -H,  0, t);  delay(60);     // hips held left, LEFT foot steps
    glideOffset(-H,  0, -H,  0, t);  delay(150);
    glideOffset(-H,  0, -H, +F, t);  delay(60);     // RIGHT foot shuffles round
    glideOffset(-H,  0, -H,  0, t);  delay(150);
    yield();
  }
  glideNeutral();
}
void turnRight(int steps) {
  int H = hipSwing, F = footTilt, t = stepPeriod / 3;
  for (int s = 0; s < steps; s++) {
    glideOffset(+H,  0, +H, +F, t);  delay(60);     // hips held right, RIGHT foot steps
    glideOffset(+H,  0, +H,  0, t);  delay(150);
    glideOffset(+H, +F, +H,  0, t);  delay(60);     // LEFT foot shuffles round
    glideOffset(+H,  0, +H,  0, t);  delay(150);
    yield();
  }
  glideNeutral();
}

// Forward-declare so dance() can call it
void drawFaceNow();

// =============================================================
//  Dance routines  (5 smooth variants, round-robin)
// =============================================================

// Helper: run server + screen tick during a pause (keeps face animated)
void dPause(int ms) {
  unsigned long t = millis();
  while (millis() - t < (unsigned long)ms) {
    yield();   // NOT server.handleClient() - see notes
    delay(4);
  }
}

// All beat durations below are whole multiples of SERVO_FRAME_MS (20 ms) so a
// glide never has to round its step count down and land short.
//
// They are also kept above the servos' physical slew rate. An SG90 does about
// 60°/100 ms unloaded — carrying the robot it is slower. The previous timings
// asked for a 44° hip reversal in 140 ms, which the servo simply cannot do: it
// saturates, the next command reverses before it ever arrives, and the leg buzzes
// in place at full stall current instead of dancing. Amplitudes are trimmed for
// the same reason.

// ── 1. Bounce — energetic up/down hip wiggle ──────────────────
// All offsets below are RELATIVE to the calibrated home pose (homeAngle) via
// glideOffset(), so the whole expression library re-centres itself the moment a
// new idle pose is saved. glideOffset already eases (cosine) and frame-locks to
// 20 ms, which is what keeps them fluent rather than buzzing.
void danceBounce(int beats) {
  int H = 18, F = 14;
  for (int i = 0; i < beats; i++) {
    glideOffset(+H,  +F,  -H,  -F,  260);
    glideOffset(-H,  -F,  +H,  +F,  260);
  }
  glideNeutral();
}

// ── 2. Moonwalk — slow glide: one foot slides, hip shifts ─────
void danceMoonwalk(int beats) {
  // Plant left foot, glide right leg forward then swap
  for (int i = 0; i < beats; i++) {
    // Phase A: right foot tilts back, hips shift right
    glideOffset(+18, 0,    -18, +12, 320);
    glideOffset(+18, -10,  -18, +12, 180);
    // Phase B: weight transfer, left side mirrors
    glideOffset(-18, -12,  +18, 0,   320);
    glideOffset(-18, -12,  +18, +10, 180);
  }
  glideNeutral();
}

// ── 3. Shimmy — rapid alternating shoulder shrugs ─────────────
void danceShimmy(int beats) {
  int S = 20;
  for (int i = 0; i < beats * 2; i++) {
    int side = (i % 2 == 0) ? 1 : -1;
    glideOffset(S*side, 6*side, -S*side, -6*side, 200);
  }
  glideNeutral();
}

// ── 4. Spin (Twist) — twist hips left-right in rhythm ─────────
void danceSpin(int beats) {
  int T = 22;
  for (int i = 0; i < beats; i++) {
    // both feet same side, hips opposite = twist
    glideOffset(+T, +10, +T, +10, 280);
    glideOffset(-T, -10, -T, -10, 280);
  }
  glideNeutral();
}

// ── 5. Wave Bow — side sway + dip like a wave ─────────────────
void danceWave(int beats) {
  for (int i = 0; i < beats; i++) {
    // lean left with dip
    glideOffset(+20, -12, +8,  +6,  300);
    glideOffset(+20, -12, -8,  +6,  180);
    // come up and lean right
    glideOffset(-8,  +6,  -20, -12, 300);
    glideOffset(+8,  +6,  -20, -12, 180);
  }
  glideNeutral();
}

// ── 6. Happy dance — an energetic choreographed routine ───────
// A little story: two eager bounces, a side-to-side wiggle, a twirl,
// then a big pop finish. All home-relative and eased, so it stays clean.
void danceHappy(int beats) {
  for (int i = 0; i < beats; i++) {
    // two eager bounces
    glideOffset(+26, +20, +26, +20, 240);  glideOffset(0, 0, 0, 0, 180);
    glideOffset(+26, +20, +26, +20, 240);  glideOffset(0, 0, 0, 0, 180);
    // quick side-to-side wiggle
    glideOffset(+24, +10, -24, -10, 200);  glideOffset(-24, -10, +24, +10, 200);
    glideOffset(+24, +10, -24, -10, 200);  glideOffset(-24, -10, +24, +10, 200);
    // a little twirl (hips together, feet lift)
    glideOffset(+28, +16, +28, +16, 260);  glideOffset(-28, -16, -28, -16, 260);
    // big pop finish, then settle
    glideOffset(+32, +26, +32, +26, 240);  glideNeutral(320);
  }
  glideNeutral();
}

// ── 7. Groove — a smooth, cool mixed routine ──────────────────
// Flowing: a lazy sway, into a twist, into a rippling leg wave. Slower
// glides so it reads as smooth and deliberate rather than frantic.
void danceGroove(int beats) {
  for (int i = 0; i < beats; i++) {
    // lazy weight-shift sway
    glideOffset(+20, -12, +12, +8,  360);  glideOffset(+12, +8,  +20, -12, 360);
    glideOffset(-12, +8,  -20, -12, 360);  glideOffset(-20, -12, -12, +8,  360);
    // smooth twist
    glideOffset(+24, +10, +24, +10, 320);  glideOffset(-24, -10, -24, -10, 320);
    // rippling leg wave around the four joints
    glideOffset(+22, 0,   0,   0,   200);  glideOffset(0,   +22, 0,   0,   200);
    glideOffset(0,   0,   +22, 0,   200);  glideOffset(0,   0,   0,   +22, 200);
    glideNeutral(300);
  }
  glideNeutral();
}

// =============================================================
//  IMU-driven movement routines
//  These use the live pitch/roll to animate servos in real-time.
//  Each runs for a fixed duration (ms), reading imu_ax/ay/az live
//  every SERVO_FRAME_MS so the motion actually responds to tilt.
// =============================================================

// Clamp a value to [-limit, limit]
static int clampI(int v, int limit) { return v < -limit ? -limit : (v > limit ? limit : v); }

// ── Lean — hips and feet follow pitch/roll orientation live ──
// Bob leans his body in the direction he is being tilted.
// pitch>0 = forward lean, roll>0 = right lean
void moveLean(int durationMs) {
  unsigned long end = millis() + (unsigned long)durationMs;
  while ((long)(millis() - end) < 0) {
    // Relative pitch/roll from calibration baseline (zero when calibrated)
    float pitch = 0.0f, roll = 0.0f;
    if (imuCalValid) {
      float p, r;
      accelPitchRoll(imu_ax, imu_ay, imu_az, &p, &r);
      pitch = angleDelta(p, imuCalPitch);
      roll  = angleDelta(r, imuCalRoll);
    }
    int hipBias   = clampI((int)(pitch * 0.6f), 30);   // forward pitch → hip lean
    int footBias  = clampI((int)(roll  * 0.5f), 25);   // side roll → foot tilt
    setServoSmooth(CH_LH, homeAngle(CH_LH) + hipBias);
    setServoSmooth(CH_RH, homeAngle(CH_RH) + hipBias);
    setServoSmooth(CH_LF, homeAngle(CH_LF) + footBias);
    setServoSmooth(CH_RF, homeAngle(CH_RF) - footBias);
    delay(SERVO_FRAME_MS);
    yield();
  }
  glideNeutral(200);
}

// ── Balance — continuously corrects posture against tilt ─────
// Bob actively compensates for tilt: if pitched forward he adjusts
// hips back, like a self-balancing bot. Great to show IMU live.
void moveBalance(int durationMs) {
  unsigned long end = millis() + (unsigned long)durationMs;
  while ((long)(millis() - end) < 0) {
    float pitch = 0.0f, roll = 0.0f;
    if (imuCalValid) {
      float p, r;
      accelPitchRoll(imu_ax, imu_ay, imu_az, &p, &r);
      pitch = angleDelta(p, imuCalPitch);
      roll  = angleDelta(r, imuCalRoll);
    }
    // Opposite sign to lean: compensate for the tilt
    int hipCorr  = clampI(-(int)(pitch * 0.8f), 30);
    int footCorr = clampI(-(int)(roll  * 0.7f), 25);
    setServoSmooth(CH_LH, homeAngle(CH_LH) + hipCorr);
    setServoSmooth(CH_RH, homeAngle(CH_RH) + hipCorr);
    setServoSmooth(CH_LF, homeAngle(CH_LF) + footCorr);
    setServoSmooth(CH_RF, homeAngle(CH_RF) - footCorr);
    delay(SERVO_FRAME_MS);
    yield();
  }
  glideNeutral(200);
}

// ── TiltWalk — walking gait where stride length scales with pitch ─
// Tilt forward for bigger steps, stay level for a gentle shuffle.
void moveTiltWalk(int steps) {
  for (int s = 0; s < steps; s++) {
    float pitch = 0.0f;
    if (imuCalValid) {
      float p, r;
      accelPitchRoll(imu_ax, imu_ay, imu_az, &p, &r);
      pitch = angleDelta(p, imuCalPitch);
    }
    // Map pitch 0..30° → hipSwing 8..35°, footTilt 6..22°
    int hswing = constrain((int)(8 + pitch * 0.9f), 8, 35);
    int ftilt  = constrain((int)(6 + pitch * 0.53f), 6, 22);
    int h = stepPeriod / 2;
    setServoOffset(CH_LF, +ftilt);  setServoOffset(CH_RF, -ftilt);  delay(h/4);
    setServoOffset(CH_LH, +hswing); setServoOffset(CH_RH, -hswing); delay(h-h/4);
    setServoOffset(CH_LF, -ftilt);  setServoOffset(CH_RF, +ftilt);  delay(h/4);
    setServoOffset(CH_LH, -hswing); setServoOffset(CH_RH, +hswing); delay(h-h/4);
    yield();
  }
  standNeutral();
}

// ── Swagger — side-to-side roll strut, hip amplitude from roll ─
// Like a confident walk — hips sway wide if tilted sideways.
void moveSwagger(int beats) {
  for (int i = 0; i < beats; i++) {
    float roll = 0.0f;
    if (imuCalValid) {
      float p, r;
      accelPitchRoll(imu_ax, imu_ay, imu_az, &p, &r);
      roll = angleDelta(r, imuCalRoll);
    }
    int amp = constrain(18 + (int)(fabsf(roll) * 0.5f), 18, 35);
    glideOffset(+amp, +amp/2, -amp, -amp/2, 280);
    glideOffset(-amp, -amp/2, +amp, +amp/2, 280);
  }
  glideNeutral();
}

// ── Sway — gentle pendulum side-sway, speed from gyro magnitude ─
// Bob sways left-right rhythmically; speed and width track motion.
void moveSway(int beats) {
  for (int i = 0; i < beats; i++) {
    // Gyro Z gives rotation speed; use it to modulate the sway tempo
    int gzAbs  = abs((int)imu_gz - (imuCalValid ? (int)imuCalGz : 0));
    int tempo  = constrain(300 - gzAbs / 4, 180, 340);
    glideOffset(+22, -10, -12, +8,  tempo);
    glideOffset(-12, +8,  +22, -10, tempo);
  }
  glideNeutral();
}

// GET /imupose — returns the current pitch/roll as servo-ready
// hip and foot offsets so the UI can preview live tilt mapping.
void handleImuPose() {
  float pitch = 0.0f, roll = 0.0f;
  if (imuCalValid) {
    float p, r;
    accelPitchRoll(imu_ax, imu_ay, imu_az, &p, &r);
    pitch = angleDelta(p, imuCalPitch);
    roll  = angleDelta(r, imuCalRoll);
  }
  int hipBias  = clampI((int)(pitch * 0.6f), 30);
  int footBias = clampI((int)(roll  * 0.5f), 25);
  String j = "{\"pitch\":"; j += String(pitch, 1);
  j += ",\"roll\":";        j += String(roll,  1);
  j += ",\"hipBias\":";     j += hipBias;
  j += ",\"footBias\":";    j += footBias;
  j += ",\"calOk\":";       j += imuCalValid ? 1 : 0;
  j += ",\"ax\":";          j += (int)imu_ax;
  j += ",\"ay\":";          j += (int)imu_ay;
  j += ",\"az\":";          j += (int)imu_az;
  j += "}";
  server.send(200, "application/json", j);
}

// ── Dance dispatcher — cycles through 5 routines ─────────────
static uint8_t danceRoutine = 0;

// Run one routine with the dance face up and the legs squared away first.
void danceOne(uint8_t routine, int times) {
  danceMode  = true;
  prevExpr   = 255;
  danceFrame = 0;

  // The display is initialized automatically at boot before motion is enabled.
  drawFaceNow();

  // Actually glide to neutral before the first beat. The old code just
  // *asserted* servoCur = 90 without moving, so if the legs happened to be
  // parked elsewhere the first glide interpolated from a lie and lurched.
  glideNeutral();

  switch (routine) {
    case 0: danceBounce(times);    break;
    case 1: danceMoonwalk(times/2 + 1); break;
    case 2: danceShimmy(times);    break;
    case 3: danceSpin(times);      break;
    case 4: danceWave(times/2 + 1); break;
    case 5: danceHappy(times/4 + 1); break;
    case 6: danceGroove(times/4 + 1); break;
  }

  glideNeutral();
  danceMode = false;
  prevExpr  = 255;
}

void dance(int times) {
  danceOne(danceRoutine % 7, times);
  danceRoutine++;
}

// =============================================================
//  TFT — drawing primitives
// =============================================================

void fillEllipse(int cx, int cy, int rx, int ry, uint16_t col) {
  for (int dy = -ry; dy <= ry; dy++) {
    float f = 1.0f - ((float)(dy*dy)) / ((float)(ry*ry));
    int   w = (int)(rx * sqrtf(f < 0 ? 0 : f));
    if (w > 0) tft.drawFastHLine(cx - w, cy + dy, w*2+1, col);
  }
}

// ── Eyes ─────────────────────────────────────────────────────
void drawEyeOn(Adafruit_GFX& surface, int cx, int cy, bool wink, int8_t ox) {
  surface.fillRect(cx-EYE_R-3, cy-EYE_R-3, (EYE_R+3)*2+1, (EYE_R+3)*2+1, C_BG);
  if (wink) {
    for (int t = -2; t <= 2; t++)
      surface.drawFastHLine(cx-EYE_R+5, cy+t, (EYE_R-5)*2, C_EYE);
    surface.fillCircle(cx-EYE_R+6, cy, 3, C_EYE);
    surface.fillCircle(cx+EYE_R-6, cy, 3, C_EYE);
  } else {
    surface.fillCircle(cx, cy, EYE_R, C_EYE);
    surface.fillCircle(cx+EYE_R/3+ox, cy-EYE_R/3, SHINE_R, C_SHINE);
  }
}

void drawEye(int cx, int cy, bool wink, int8_t ox) {
  drawEyeOn(tft,cx,cy,wink,ox);
}

// ── Sleepy eye: heavy drooping eyelid covers 65% of eye ───────
// Looks like 😴 — only bottom 35% of black eye visible under a thick lid
void drawSleepyEye(int cx, int cy) {
  int r = EYE_R;
  tft.fillRect(cx-r-3, cy-r-3, (r+3)*2+1, (r+3)*2+1, C_BG);
  // draw the full eye circle first
  tft.fillCircle(cx, cy, r, C_EYE);
  // tiny white catchlight (lower since lid covers top)
  tft.fillCircle(cx+r/4, cy+r/4, SHINE_R-1, C_SHINE);
  // thick white eyelid covers top 65%
  int lidH = (r * 13) / 10;  // 1.3× radius height = covers 65%
  tft.fillRect(cx-r-2, cy-r-2, (r+2)*2+1, lidH, C_BG);
  // thick curved lid edge — 4px thick droopy line
  for (int t = 0; t <= 3; t++)
    tft.drawFastHLine(cx-r+2, cy-r+lidH-4+t, (r-2)*2, C_EYE);
  // droop: corners dip down for that sleepy curve
  tft.fillCircle(cx-r+3, cy-r+lidH-1, 3, C_EYE);
  tft.fillCircle(cx+r-3, cy-r+lidH-1, 3, C_EYE);
}

// ── Surprised eye: HUGE pop-out with concentric rings — like 🤯
void drawSurprisedEye(int cx, int cy) {
  int r = EYE_R + 5;  // bigger than before
  tft.fillRect(cx-r-4, cy-r-4, (r+4)*2+1, (r+4)*2+1, C_BG);
  // outer shock ring (white gap)
  tft.drawCircle(cx, cy, r+2, C_EYE);
  tft.drawCircle(cx, cy, r+3, C_EYE);
  // main large eye
  tft.fillCircle(cx, cy, r, C_EYE);
  // large bright catchlight
  tft.fillCircle(cx+r/3, cy-r/3, SHINE_R+2, C_SHINE);
}

// ── Angry eye: hard downward glare, upper lid slanted toward the nose ──
// The eye is a black ball with a heavy white "lid" wedge cut across the top —
// deep on the temple (outer) side, shallow on the nose (inner) side — so the
// visible slit points down-and-inward at the centre, the classic scowl.
// Catchlight sits low, a red bloodshot flick in the inner corner. Mirror-aware
// via the eye's x position.
void drawAngryEye(int cx, int cy) {
  bool L = (cx < MOUTH_CX);                  // left eye? nose (inner) side is +x
  int  R = EYE_R;
  int  innerX = L ? cx + R + 3 : cx - R - 3; // toward the nose
  int  outerX = L ? cx - R - 3 : cx + R + 3; // toward the temple
  tft.fillRect(cx-R-3, cy-R-3, (R+3)*2+1, (R+3)*2+1, C_BG);
  tft.fillCircle(cx, cy, R, C_EYE);
  // Catchlight low + toward the nose so the glare points down-and-in.
  tft.fillCircle(cx + (L ? 5 : -5), cy + 6, SHINE_R - 1, C_SHINE);
  // Angry lid: white wedge biting from the top down to eye-centre on the temple
  // side, tapering to nothing on the nose side — the visible slit points inward
  // and down, the classic scowl.
  tft.fillTriangle(innerX, cy-R-3, outerX, cy-R-3, outerX, cy, C_BG);
  tft.fillRect(cx-R-3, cy-R-3, (R+3)*2+1, 6, C_BG);   // flatten the very top
  // Bloodshot flick in the inner corner.
  int vx = L ? cx + R - 5 : cx - R + 5;
  tft.drawLine(vx, cy - 3, cx + (L ? 4 : -4), cy + 2, C_FIRE1);
  tft.drawLine(vx, cy - 2, cx + (L ? 4 : -4), cy + 3, C_FIRE1);
}

void drawHeartEye(int cx, int cy) {
  tft.fillRect(cx-EYE_R-3, cy-EYE_R-3, (EYE_R+3)*2+1, (EYE_R+3)*2+1, C_BG);
  int r = 9;
  tft.fillCircle(cx-r/2-1, cy-2, r/2+2, C_HEART);
  tft.fillCircle(cx+r/2+1, cy-2, r/2+2, C_HEART);
  for (int dy = 0; dy <= r+2; dy++) {
    int hw = (r+2)-dy;
    if (hw > 0) tft.drawFastHLine(cx-hw, cy-2+dy, hw*2+1, C_HEART);
  }
  tft.fillCircle(cx-3, cy-3, 2, C_SHINE);
}

// ── Eyebrows ─────────────────────────────────────────────────
void drawBrowFlat(int cx, int cy) {
  for (int t = 0; t <= 2; t++) tft.drawFastHLine(cx-14, cy+t, 28, C_BROW);
}
// Angry brows form a hard V driving down into the centre: temple end high, nose
// end low, thick (5 px), pinched close so the inner ends nearly meet over the
// nose. (The old angry brows were an exact copy of the SAD brows — inner ends
// raised — which is why the face never really looked angry.)
void drawBrowAngryL(int cx, int cy) {          // left brow "\"  — nose end low
  for (int t = 0; t <= 4; t++)
    tft.drawLine(cx-16, cy-5+t, cx+16, cy+6+t, C_BROW);
}
void drawBrowAngryR(int cx, int cy) {          // right brow "/" — nose end low
  for (int t = 0; t <= 4; t++)
    tft.drawLine(cx-16, cy+6+t, cx+16, cy-5+t, C_BROW);
}
void drawBrowSadL(int cx, int cy) {
  tft.drawLine(cx-14, cy,   cx+14, cy+4, C_BROW);
  tft.drawLine(cx-14, cy+1, cx+14, cy+5, C_BROW);
  tft.drawLine(cx-14, cy+2, cx+14, cy+6, C_BROW);
}
void drawBrowSadR(int cx, int cy) {
  tft.drawLine(cx-14, cy+4, cx+14, cy,   C_BROW);
  tft.drawLine(cx-14, cy+5, cx+14, cy+1, C_BROW);
  tft.drawLine(cx-14, cy+6, cx+14, cy+2, C_BROW);
}

// ── Mouths ───────────────────────────────────────────────────
void drawSmileArc(int rx, int ry, int thick) {
  int cx = MOUTH_CX, cy = MOUTH_Y - ry;
  for (int dx = -rx; dx <= rx; dx++) {
    float f = 1.0f - ((float)(dx*dx)) / ((float)(rx*rx));
    int y = cy + (int)(ry * sqrtf(f < 0 ? 0 : f));
    for (int t = 0; t < thick; t++) tft.drawPixel(cx+dx, y+t, C_MOUTH);
  }
  tft.fillCircle(cx-rx+2, cy+ry-2, thick-1, C_MOUTH);
  tft.fillCircle(cx+rx-2, cy+ry-2, thick-1, C_MOUTH);
}
void drawSmileHappy()  { drawSmileArc(44, 14, 3); }
void drawSmileSmall()  { drawSmileArc(28, 10, 3); }
void drawSmileLove()   { drawSmileArc(44, 16, 4); }

void drawFrown() {
  int cx = MOUTH_CX, cy = MOUTH_Y+4, rx = 36, ry = 12;
  for (int dx = -rx; dx <= rx; dx++) {
    float f = 1.0f - ((float)(dx*dx)) / ((float)(rx*rx));
    int y = cy - (int)(ry * sqrtf(f < 0 ? 0 : f));
    for (int t = 0; t < 3; t++) tft.drawPixel(cx+dx, y+t, C_MOUTH);
  }
  tft.fillCircle(cx-rx+2, cy-ry+2, 2, C_MOUTH);
  tft.fillCircle(cx+rx-2, cy-ry+2, 2, C_MOUTH);
}

// 😡 Angry mouth: jagged gritted teeth — growling snarl
// Gritted-teeth snarl: a solid black mouth block with white teeth clenched
// across it (vertical gaps + a centre bite line), corners dragged down hard
// for a scowl. Reads far more "angry" than the old single zig-zag lip.
void drawMouthAngryOn(Adafruit_GFX& surface, int cx, int y) {
  int hw = 30, h = 12;
  surface.fillRect(cx-hw, y, hw*2, h, C_MOUTH);            // lips (black block)
  surface.fillRect(cx-hw+3, y+3, hw*2-6, 6, C_BG);         // clenched teeth (white)
  for (int gx = -hw+3; gx <= hw-3; gx += 6)
    surface.drawFastVLine(cx+gx, y+3, 6, C_MOUTH);         // gaps between teeth
  surface.drawFastHLine(cx-hw+3, y+5, hw*2-6, C_MOUTH);    // upper/lower bite line
  // Down-pulled scowl corners.
  surface.fillTriangle(cx-hw, y, cx-hw, y+h, cx-hw-6, y+h+4, C_MOUTH);
  surface.fillTriangle(cx+hw, y, cx+hw, y+h, cx+hw+6, y+h+4, C_MOUTH);
}

void drawMouthAngry() {
  drawMouthAngryOn(tft,MOUTH_CX,MOUTH_Y-4);
}

void drawMouthO() {
  fillEllipse(MOUTH_CX, MOUTH_Y-4, 18, 12, C_MOUTH);
  fillEllipse(MOUTH_CX, MOUTH_Y-4, 12,  7, C_BG);
}

// Yawn mouth: open = 0..20. At peak: wide open O with teeth visible — like 😴
void drawMouthYawn(uint8_t open) {
  tft.fillRect(0, MOUTH_Y - 22, SCR_W, 36, C_BG);
  if (open < 3) {
    // closed: small flat line
    for (int t = 0; t <= 2; t++) tft.drawFastHLine(MOUTH_CX-16, MOUTH_Y+t, 32, C_MOUTH);
  } else {
    int rx = 14 + open, ry = 4 + open/2;
    // outer black mouth (open oval)
    fillEllipse(MOUTH_CX, MOUTH_Y-2, rx, ry, C_MOUTH);
    // inner white (throat) — smaller gap
    fillEllipse(MOUTH_CX, MOUTH_Y-2, rx-3, ry-2, C_BG);
    // teeth bar across middle when open enough
    if (open > 8) {
      int tw = rx - 4;
      for (int t = 0; t <= 2; t++)
        tft.drawFastHLine(MOUTH_CX-tw, MOUTH_Y-3+t, tw*2, C_MOUTH);
      // tooth dividers
      for (int d = -1; d <= 1; d++)
        tft.drawFastVLine(MOUTH_CX + d*10, MOUTH_Y-3, 3, C_EYE);
    }
  }
}

void drawMouthGrin() {
  int cx = MOUTH_CX, cy = MOUTH_Y-14, rx = 46, ry = 16;
  for (int dx = -rx; dx <= rx; dx++) {
    float f = 1.0f - ((float)(dx*dx)) / ((float)(rx*rx));
    int y = cy + (int)(ry * sqrtf(f < 0 ? 0 : f));
    for (int t = 0; t < 2; t++) tft.drawPixel(cx+dx, y+t, C_MOUTH);
  }
  tft.drawFastHLine(cx-rx, cy+ry+2, rx*2, C_MOUTH);
  tft.fillRect(cx-rx+1, cy+ry-3, rx*2-1, 6, C_BG);
  for (int tx = -2; tx <= 2; tx++)
    tft.drawFastVLine(cx+tx*14, cy+ry-3, 6, C_MOUTH);
  tft.fillCircle(cx-rx+3, cy+ry-1, 2, C_MOUTH);
  tft.fillCircle(cx+rx-3, cy+ry-1, 2, C_MOUTH);
}

// Normal mode: small relaxed smile — just like the original Bob
void drawMouthNormalOn(Adafruit_GFX& surface, int cx, int cy, float bank) {
  // Small gentle upward arc — not as wide as Happy, not flat
  int rx = 32, ry = 9;
  for (int dx = -rx; dx <= rx; dx++) {
    float f = 1.0f - ((float)(dx*dx)) / ((float)(rx*rx));
    int y = cy + (int)(ry * sqrtf(f < 0 ? 0 : f) + dx*bank);
    for (int t = 0; t < 3; t++) surface.drawPixel(cx+dx, y+t, C_MOUTH);
  }
  surface.fillCircle(cx-rx+2, cy+ry-2-(int)(rx*bank), 2, C_MOUTH);
  surface.fillCircle(cx+rx-2, cy+ry-2+(int)(rx*bank), 2, C_MOUTH);
}

void drawMouthNormal() {
  drawMouthNormalOn(tft,MOUTH_CX,MOUTH_Y-6,0);
}

// ── Extras ───────────────────────────────────────────────────
void drawBlush() {
  fillEllipse(EYE_LX+EYE_R+6, EYE_Y+EYE_R-2, 10, 5, C_BLUSH);
  fillEllipse(EYE_RX-EYE_R-6, EYE_Y+EYE_R-2, 10, 5, C_BLUSH);
}

// drawStarEye — col: ray/centre colour, bg: background colour for clear rect
void drawStarEye(int cx, int cy, int rot, uint16_t col, uint16_t bg) {
  int R = EYE_R + 4;  // slightly larger than normal
  tft.fillRect(cx-R-4, cy-R-4, (R+4)*2+1, (R+4)*2+1, bg);
  // outer glow ring
  tft.drawCircle(cx, cy, R+2, col);
  tft.drawCircle(cx, cy, R+1, col);
  // 5-pointed star rays — 3px thick
  for (int p = 0; p < 5; p++) {
    float a1 = (p*72+rot)*3.14159f/180.0f;
    float a2 = ((p+2)*72+rot)*3.14159f/180.0f;
    int x1 = cx+(int)(R*cosf(a1)), y1 = cy+(int)(R*sinf(a1));
    int x2 = cx+(int)(R*cosf(a2)), y2 = cy+(int)(R*sinf(a2));
    for (int t = -1; t <= 1; t++) {
      tft.drawLine(x1+t, y1, x2+t, y2, col);
      tft.drawLine(x1, y1+t, x2, y2+t, col);
    }
  }
  tft.fillCircle(cx, cy, 6, col);
  tft.fillCircle(cx, cy, 3, bg);  // dark pupil centre for depth
}

// Full rainbow dance face — fills screen, cycles colour every frame
void drawDanceFace() {
  uint8_t ci  = danceFrame % DANCE_COLORS;
  uint16_t bg = DANCE_BG[ci];
  uint16_t fg = DANCE_FG[ci];
  int rot = (danceFrame % 2 == 0) ? 0 : 36;

  tft.fillScreen(bg);

  // Star eyes with matching foreground
  drawStarEye(EYE_LX, EYE_Y, rot, fg, bg);
  drawStarEye(EYE_RX, EYE_Y, rot, fg, bg);

  // Eyebrows — thick flat, fg colour
  for (int t = 0; t <= 3; t++) {
    tft.drawFastHLine(EYE_LX-15, EYE_Y-EYE_R-11+t, 30, fg);
    tft.drawFastHLine(EYE_RX-15, EYE_Y-EYE_R-11+t, 30, fg);
  }

  // Grin — fg colour (reuse drawMouthGrin logic inline with fg)
  int cx = MOUTH_CX, cy = MOUTH_Y-14, rx = 46, ry = 16;
  for (int dx = -rx; dx <= rx; dx++) {
    float f = 1.0f - ((float)(dx*dx)) / ((float)(rx*rx));
    int y = cy + (int)(ry * sqrtf(f < 0 ? 0 : f));
    for (int t = 0; t < 2; t++) tft.drawPixel(cx+dx, y+t, fg);
  }
  tft.drawFastHLine(cx-rx, cy+ry+2, rx*2, fg);
  tft.fillRect(cx-rx+1, cy+ry-3, rx*2-1, 6, bg);
  for (int tx2 = -2; tx2 <= 2; tx2++)
    tft.drawFastVLine(cx+tx2*14, cy+ry-3, 6, fg);
  tft.fillCircle(cx-rx+3, cy+ry-1, 2, fg);
  tft.fillCircle(cx+rx-3, cy+ry-1, 2, fg);

  drawBars();   // the rainbow fillScreen above wiped them
  danceFrame++;
}

// Both clamp to the face area — the raw rects would have reached into the
// status bars (brows start at y=8, the mouth erase ran to the last row).
void eraseBrows() {
  int y0 = EYE_Y-EYE_R-22;
  if (y0 < FACE_TOP) y0 = FACE_TOP;
  tft.fillRect(0, y0, SCR_W, (EYE_Y-EYE_R) - y0, C_BG);
}
void eraseMouth() {
  int y0 = EYE_Y+EYE_R+6;
  tft.fillRect(0, y0, SCR_W, (FACE_BOT + 1) - y0, C_BG);
}

// =============================================================
//  Per-expression animation helpers
// =============================================================

// ── ANGRY: fire + smoke ───────────────────────────────────────
// life caps at 13 so a flame rising 1 px/tick from y=30 stops at y=17 —
// its 3 px radius then still clears FACE_TOP instead of hitting the top bar.
void initFire() {
  for (int i = 0; i < 6; i++) {
    fireL[i] = {(int8_t)(EYE_LX-8+random(16)), (int8_t)(EYE_Y-EYE_R), (uint8_t)random(6,14)};
    fireR[i] = {(int8_t)(EYE_RX-8+random(16)), (int8_t)(EYE_Y-EYE_R), (uint8_t)random(6,14)};
    smoke[i<3?i:0] = {(int8_t)(MOUTH_CX-10+random(20)), (int8_t)(EYE_Y+EYE_R+8), (uint8_t)3, (uint8_t)random(10,20)};
  }
  fireInit = true;
}

void tickFire() {
  for (int i = 0; i < 6; i++) {
    // erase old
    tft.fillCircle(fireL[i].x, fireL[i].y, 3, C_BG);
    tft.fillCircle(fireR[i].x, fireR[i].y, 3, C_BG);
    // move up
    fireL[i].y--; fireR[i].y--;
    fireL[i].life--; fireR[i].life--;
    // respawn
    if (fireL[i].life == 0) { fireL[i] = {(int8_t)(EYE_LX-8+random(16)), (int8_t)(EYE_Y-EYE_R-1), (uint8_t)random(6,14)}; }
    if (fireR[i].life == 0) { fireR[i] = {(int8_t)(EYE_RX-8+random(16)), (int8_t)(EYE_Y-EYE_R-1), (uint8_t)random(6,14)}; }
    // draw — colour by age
    uint16_t cl = (fireL[i].life > 12) ? C_FIRE3 : (fireL[i].life > 6 ? C_FIRE2 : C_FIRE1);
    uint16_t cr = (fireR[i].life > 12) ? C_FIRE3 : (fireR[i].life > 6 ? C_FIRE2 : C_FIRE1);
    tft.fillCircle(fireL[i].x, fireL[i].y, 3, cl);
    tft.fillCircle(fireR[i].x, fireR[i].y, 3, cr);
  }
  // smoke puffs from nose area (centre-bottom of face)
  for (int i = 0; i < 3; i++) {
    tft.fillCircle(smoke[i].x, smoke[i].y, smoke[i].r, C_BG);
    smoke[i].y--; smoke[i].r++; smoke[i].life--;
    if (smoke[i].life == 0 || smoke[i].r > 8) {
      smoke[i] = {(int8_t)(MOUTH_CX-8+random(16)), (int8_t)(EYE_Y+EYE_R+10+i*4), (uint8_t)2, (uint8_t)random(12,20)};
    }
    tft.fillCircle(smoke[i].x, smoke[i].y, smoke[i].r, C_SMOKE);
  }
}

// ── SAD: sliding tears ────────────────────────────────────────
void initTears() {
  tearY[0]=0; tearY[1]=-6; tearY[2]=-13; tearY[3]=-20;
  tearActive = true;
}

void tickTears() {
  // 2 tear tracks per eye: left eye left-side, right eye right-side
  int tx[4] = {EYE_LX-5, EYE_LX+3, EYE_RX-3, EYE_RX+5};
  for (int i = 0; i < 4; i++) {
    int baseY = EYE_Y + EYE_R;
    int ty = baseY + tearY[i];
    // erase old drop
    tft.fillRect(tx[i]-2, baseY-2, 5, 40, C_BG);
    // advance
    tearY[i] += 2;
    if (tearY[i] > 38) tearY[i] = -random(5,22);
    // draw new drop
    ty = baseY + tearY[i];
    if (ty >= baseY && ty < FACE_BOT-2) {
      tft.fillCircle(tx[i], ty,   2, C_TEAR);
      tft.fillCircle(tx[i], ty+1, 2, C_TEAR);
    }
  }
}

// ── SLEEPY: yawn + Zzz ───────────────────────────────────────
// Each Z rises 1 px/tick for `life` ticks, so spawn-y minus life must land
// at FACE_TOP+2 — that keeps the whole glyph clear of the top bar.
void initZzz() {
  zzzBubbles[0] = {(int16_t)(SCR_W-26), (int16_t)(FACE_TOP+28), 26, 2};
  zzzBubbles[1] = {(int16_t)(SCR_W-14), (int16_t)(FACE_TOP+18), 16, 1};
  zzzBubbles[2] = {(int16_t)(SCR_W-22), (int16_t)(FACE_TOP+10),  8, 1};
  zzzInit = true;
}

void tickSleepy() {
  // Yawn cycle
  tft.fillRect(0, MOUTH_Y-22, SCR_W, 32, C_BG);
  yawnOpen += yawnDir * 1;
  if (yawnOpen >= 16) yawnDir = -1;
  if (yawnOpen <= 0)  { yawnDir = 1; yawnOpen = 0; }
  drawMouthYawn(yawnOpen);

  // Zzz float up
  for (int i = 0; i < 3; i++) {
    // erase — sized to the glyph (12x16 at size 2, 6x8 at size 1)
    tft.fillRect(zzzBubbles[i].x-2, zzzBubbles[i].y-2,
                 zzzBubbles[i].sz*12+4, zzzBubbles[i].sz*8+4, C_BG);
    zzzBubbles[i].life--;
    if (zzzBubbles[i].life == 0) {
      // respawn
      static const int16_t sx[3] = {(int16_t)(SCR_W-26),(int16_t)(SCR_W-14),(int16_t)(SCR_W-22)};
      static const int16_t sy[3] = {FACE_TOP+28, FACE_TOP+18, FACE_TOP+10};
      static const uint8_t sl[3] = {26, 16, 8};
      zzzBubbles[i] = {sx[i], sy[i], sl[i], (uint8_t)(i==0?2:1)};
    }
    zzzBubbles[i].y--;
    // draw Z
    tft.setTextSize(zzzBubbles[i].sz);
    tft.setTextColor(C_EYE);
    tft.setCursor(zzzBubbles[i].x, zzzBubbles[i].y);
    tft.print("Z");
  }
}

// ── SURPRISED: bouncing brows ─────────────────────────────────
void tickSurprisedBrows() {
  // Erase brow area
  eraseBrows();
  browOffset += browDir * 1;
  if (browOffset <= -6) browDir = +1;
  if (browOffset >= 0)  browDir = -1;
  // base -12 (was -16): at the top of the bounce this now sits at y=12,
  // flush with FACE_TOP, instead of riding up into the status bar.
  int by = EYE_Y - EYE_R - 12 + browOffset;
  for (int t = 0; t <= 2; t++) {
    tft.drawFastHLine(EYE_LX-14, by+t, 28, C_BROW);
    tft.drawFastHLine(EYE_RX-14, by+t, 28, C_BROW);
  }
}

// ── HAPPY: sparkle idle ───────────────────────────────────────
void initSparks() {
  for (int i = 0; i < 5; i++)
    sparks[i] = {(int16_t)random(4, SCR_W-4), (int16_t)random(EYE_Y+EYE_R+8, FACE_BOT-2), (uint8_t)random(4,12)};
  sparkInit = true;
}

void tickSparks() {
  for (int i = 0; i < 5; i++) {
    tft.drawPixel(sparks[i].x,   sparks[i].y,   C_BG);
    tft.drawPixel(sparks[i].x+1, sparks[i].y,   C_BG);
    tft.drawPixel(sparks[i].x,   sparks[i].y+1, C_BG);
    tft.drawPixel(sparks[i].x+1, sparks[i].y+1, C_BG);
    sparks[i].life--;
    if (sparks[i].life == 0) {
      sparks[i] = {(int16_t)random(4,SCR_W-4),(int16_t)random(EYE_Y+EYE_R+8,FACE_BOT-2),(uint8_t)random(4,12)};
    }
    uint16_t sc = (sparks[i].life % 2 == 0) ? C_SPARK : C_BLUSH;
    tft.drawPixel(sparks[i].x,   sparks[i].y,   sc);
    tft.drawPixel(sparks[i].x+1, sparks[i].y,   sc);
    tft.drawPixel(sparks[i].x,   sparks[i].y+1, sc);
    tft.drawPixel(sparks[i].x+1, sparks[i].y+1, sc);
  }
}

// ── LOVE: floating mini hearts ───────────────────────────────
void initHearts() {
  for (int i = 0; i < 4; i++)
    hearts[i] = {(int16_t)random(10, SCR_W-10), (int16_t)(FACE_BOT-8-i*12), (uint8_t)random(15,30), 1};
  heartFloatInit = true;
}

void tickHearts() {
  for (int i = 0; i < 4; i++) {
    // erase
    tft.fillRect(hearts[i].x-6, hearts[i].y-6, 14, 14, C_BG);
    hearts[i].life--;
    hearts[i].y -= 1;
    // FACE_TOP+6, not 2: the erase rect reaches 6 px above y, so respawning
    // any higher would clear a strip out of the top bar.
    if (hearts[i].life == 0 || hearts[i].y < FACE_TOP+6) {
      hearts[i] = {(int16_t)random(10,SCR_W-10),(int16_t)(FACE_BOT-8),(uint8_t)random(15,30),1};
    }
    // draw tiny heart
    int hx = hearts[i].x, hy = hearts[i].y;
    tft.fillCircle(hx-2, hy-1, 3, C_HEART);
    tft.fillCircle(hx+2, hy-1, 3, C_HEART);
    for (int dy=0; dy<=5; dy++) {
      int hw = 4-dy; if(hw<1)hw=1;
      tft.drawFastHLine(hx-hw, hy+dy, hw*2+1, C_HEART);
    }
  }
}

// =============================================================
//  LISTENING face — shown whenever the mic is live
//
//  Layout (landscape 160×128, face area y=12..115):
//
//    Centre: big microphone icon (circle body + grille lines + stand)
//    Left side: 3 sound-wave arcs pulsing outward from the mic
//    Right side: 3 sound-wave arcs pulsing outward from the mic
//    Label: "LISTENING" centred below the icon, above the bottom bar
//
//  The mic icon is fully symmetric and clearly readable at a glance.
//  Sound waves radiate from both sides so it looks alive. The label
//  sits in the face area (y ≤ FACE_BOT-10), not inside the bottom bar.
// =============================================================

// Mic icon centre — horizontally centred, vertically in the upper half
#define MIC_CX  80   // screen centre X
#define MIC_CY  58   // mic capsule centre Y

// Draw the microphone body: rounded-rectangle capsule + stand + base
void drawMicIcon() {
  // ── Capsule (rounded rect, 22 wide × 32 tall) ──────────────
  int cx = MIC_CX, cy = MIC_CY;
  // Fill the body
  tft.fillRoundRect(cx - 11, cy - 16, 22, 32, 10, C_EYE);
  // Grille lines (3 horizontal slots) in background colour
  for (int row = 0; row < 3; row++) {
    int y = cy - 8 + row * 6;
    tft.fillRect(cx - 7, y, 14, 2, C_BG);
  }

  // ── Stand arc (semicircle below capsule) ──────────────────
  // drawCircleHelper mask 0xC = bottom half
  tft.drawCircleHelper(cx, cy + 16, 14, 0xC, C_EYE);
  tft.drawCircleHelper(cx, cy + 16, 15, 0xC, C_EYE);  // 2 px thick
  // Vertical stem below the arc
  tft.fillRect(cx - 1, cy + 16, 3, 8, C_EYE);
  // Horizontal base
  tft.fillRect(cx - 12, cy + 23, 25, 3, C_EYE);
}

// Sound waves: 3 arcs on each side, pulsing outward from the mic capsule.
// Each frame only ONE arc per side is lit; the others are erased to BG —
// no fillRect needed so no flicker.
void drawListenWaves() {
  int cx = MIC_CX, cy = MIC_CY;
  // Pulse travels outward: frame 0 = inner arc lit, 2 = outer
  int lit = listenFrame % 3;
  for (int k = 0; k < 3; k++) {
    uint16_t c = (k == lit) ? C_EYE : C_BG;
    int r  = 20 + k * 10;   // radii 20, 30, 40
    int r2 = r + 1;          // 2 px thick

    // Left side arcs — right half of the left semicircle (mask 0x6)
    tft.drawCircleHelper(cx, cy, r,  0x6, c);
    tft.drawCircleHelper(cx, cy, r2, 0x6, c);

    // Right side arcs — left half of the right semicircle (mask 0x9)
    tft.drawCircleHelper(cx, cy, r,  0x9, c);
    tft.drawCircleHelper(cx, cy, r2, 0x9, c);
  }
}

void drawListenFace() {
  drawMicIcon();
  drawListenWaves();
  // "LISTENING" label — centred, just above the bottom bar (y=101 leaves
  // 2 px gap before BOT_BAR_Y=116 and the 8 px text ends at y=109, clear).
  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setTextColor(C_EYE, C_BG);   // opaque so re-draws don't ghost
  tft.setCursor((SCR_W - 9 * 6) / 2, 101);
  tft.print("LISTENING");
}

// Only the waves move every 80 ms tick — the mic icon and label are static.
void tickListen() {
  listenFrame++;
  drawListenWaves();
}

// =============================================================
//  Full expression draw (called on mode change / full refresh)
// =============================================================
// =============================================================
//  Scared face — shown while the robot is reacting to being picked up
// =============================================================
// Deliberately built from small, individually-addressable pieces. The tremble
// animation below repaints ONLY the two eye squares, never the whole screen, so
// the face can shiver for the length of the reaction without a single flicker.

// ── Scared face helpers ───────────────────────────────────────

// Big panicked eye with dashed/gapped outer ring.
// ox/oy place the pupil anywhere inside the white sclera.
void drawScaredEye(int cx, int cy, int8_t ox, int8_t oy) {
  int r = EYE_R + 6;   // outer radius of the ring

  // Clear bounding box cleanly first
  tft.fillRect(cx - r - 3, cy - r - 3, (r + 3) * 2 + 1, (r + 3) * 2 + 1, C_BG);

  // White sclera (filled circle — the background of the eye)
  tft.fillCircle(cx, cy, r - 2, C_BG);

  // Dashed outer ring: draw short arcs, skip equal-length gaps.
  // 36 segments of 10° each; draw every other one → 18 dashes.
  // Two-pixel thick: draw at radius r and r-1.
  const int SEGMENTS = 36;
  const float STEP = 2.0f * PI / SEGMENTS;
  for (int seg = 0; seg < SEGMENTS; seg += 2) {   // every other = dashed
    float a0 = seg * STEP;
    float a1 = (seg + 1) * STEP;
    // Rasterise each dash as a series of pixels along the arc
    int steps = 6;
    for (int s = 0; s <= steps; s++) {
      float a = a0 + (a1 - a0) * s / steps;
      float ca = cosf(a), sa = sinf(a);
      int px0 = cx + (int)(r * ca);
      int py0 = cy + (int)(r * sa);
      int px1 = cx + (int)((r - 1) * ca);
      int py1 = cy + (int)((r - 1) * sa);
      tft.drawPixel(px0, py0, C_EYE);
      tft.drawPixel(px1, py1, C_EYE);
    }
  }

  // Pupil
  tft.fillCircle(cx + ox, cy + oy, 5, C_EYE);
  // Catchlight
  tft.fillCircle(cx + ox + 2, cy + oy - 2, 2, C_SHINE);
  // "Showing the whites" — small bare strip at the bottom of the sclera
  for (int t = 0; t <= 1; t++)
    tft.drawFastHLine(cx - r + 7, cy + r - 6 + t, (r - 7) * 2, C_BG);
}

// Brows yanked high, 4-pixel thick, steep fright arch.
void drawScaredBrows() {
  tft.fillRect(0, FACE_TOP, SCR_W, (EYE_Y - EYE_R - 4) - FACE_TOP, C_BG);
  int by = EYE_Y - EYE_R - 16;
  for (int t = 0; t <= 3; t++) {
    tft.drawLine(EYE_LX-16, by+10+t, EYE_LX+14, by+t,   C_BROW);
    tft.drawLine(EYE_RX+16, by+10+t, EYE_RX-14, by+t,   C_BROW);
  }
}

// Wide-open gasp mouth with pink tongue and dark throat.
void drawScaredMouth() {
  tft.fillRect(0, MOUTH_Y-24, SCR_W, 40, C_BG);
  fillEllipse(MOUTH_CX, MOUTH_Y,   18, 16, C_MOUTH);
  fillEllipse(MOUTH_CX, MOUTH_Y+4, 12,  9, C_HEART);
  fillEllipse(MOUTH_CX, MOUTH_Y+4,  6,  4, C_EYE);
}

// Single sweat teardrop at pixel (sx, sy).
static void drawSweatDrop(int sx, int sy) {
  tft.fillCircle(sx,    sy + 5, 5, C_TEAR);
  tft.fillTriangle(sx-4, sy+4, sx+4, sy+4, sx, sy-8, C_TEAR);
  tft.fillCircle(sx-1, sy+4, 1, C_SHINE);
}

// Draw the initial scared face (static, called once by drawFaceNow).
void drawScaredFace() {
  drawScaredBrows();
  drawScaredEye(EYE_LX, EYE_Y, 0, -7);   // pupils up-centre at start
  drawScaredEye(EYE_RX, EYE_Y, 0, -7);
  // Two sweat drops per side, staggered so the stream looks continuous
  drawSweatDrop(EYE_LX - EYE_R - 14, EYE_Y - 14);
  drawSweatDrop(EYE_LX - EYE_R - 14, EYE_Y - 1);
  drawSweatDrop(EYE_RX + EYE_R + 14, EYE_Y - 14);
  drawSweatDrop(EYE_RX + EYE_R + 14, EYE_Y - 1);
  drawScaredMouth();
}

// ── Scared animation tick ─────────────────────────────────────
// Called every 80 ms from screenTick().
// Pupils visit 8 positions in a roaming cycle — up-left, up, up-right,
// right, down-right, down, down-left, left — so the eyes look like they
// are frantically scanning for an escape route.
// Three staggered sweat drops per side flow downward continuously.
void scaredAnimTick() {
  scaredFrame++;

  // ── Pupils: 8 gaze positions, one per frame ───────────────
  //   positions arranged around the sclera (r≈18, pupil radius 5,
  //   so safe travel is ±8 px in each axis)
  static const int8_t EYE_POS_X[8] = {  -7,  0,  7,  8,  7,  0, -7, -8 };
  static const int8_t EYE_POS_Y[8] = {  -7, -8, -7,  0,  6,  7,  6,  0 };
  uint8_t step = scaredFrame % 8;
  // Left and right eye are offset by 4 steps so they look in different
  // directions simultaneously — more panicked and less robotic.
  uint8_t stepR = (step + 4) % 8;
  drawScaredEye(EYE_LX, EYE_Y, EYE_POS_X[step],  EYE_POS_Y[step]);
  drawScaredEye(EYE_RX, EYE_Y, EYE_POS_X[stepR], EYE_POS_Y[stepR]);

  // ── Sweat: 3 drops per side, staggered by 8 px each ──────
  // scaredSweatOff advances 2 px every tick → ~14 px/s at 80 ms ticks.
  // Each drop lives in a 24-pixel window (offset 0, 8, 16) so there is
  // always a drop visible somewhere on the cheek.
  scaredSweatOff = (scaredSweatOff + 2) % 24;

  const int LX  = EYE_LX - EYE_R - 14;
  const int RX  = EYE_RX + EYE_R + 14;
  const int TOP = EYE_Y - 14;          // where the stream starts
  const int H   = 38;                   // erase strip height

  // Erase the whole sweat column on both sides before redrawing
  tft.fillRect(LX - 6, TOP - 10, 14, H, C_BG);
  tft.fillRect(RX - 6, TOP - 10, 14, H, C_BG);

  // Draw 3 drops per side, each 8 px apart in the stream
  for (int d = 0; d < 3; d++) {
    int off = (scaredSweatOff + d * 8) % 24;
    int y   = TOP + off;
    if (y < TOP + H - 12) {   // only draw if within the erase strip
      drawSweatDrop(LX, y);
      drawSweatDrop(RX, y);
    }
  }
}

// Flag set by handleMove() after a blocking servo command finishes.
// screenTick() sees it and does one full fillScreen refresh to recover
// any blank caused by SPI noise during the move — without flashing during
// normal idle animation.
static bool needFullRedraw = false;

// Identity of the face currently painted on the panel. Lets drawFaceNow() tell
// two different situations apart:
//   • switching to a DIFFERENT face (another expression, or in/out of dance /
//     listen / scared) — the previous face's particles, mic icon, sound-wave
//     arcs or rainbow live in the screen's side/centre gaps that the per-zone
//     erases below never touch, so the whole face must be wiped first or those
//     fragments bleed through and "make other faces bad";
//   • merely REFRESHING the same face (heal tick / post-move repaint) — a
//     flash-free zone-erase is enough, and a full wipe here would flicker.
// 255 = nothing drawn yet, so the very first face (and the one after the black
// boot terminal) gets a clean full wipe too.
#define FID_DANCE  200
#define FID_LISTEN 201
#define FID_SCARED 202
static uint8_t lastFaceId = 255;
static bool eyeDemoActive = false;
static unsigned long eyeDemoStart = 0, eyeDemoNext = 0;


// Human-readable name for a face id — used by the on-screen-event log below.
const char* faceName(uint8_t id) {
  switch (id) {
    case FID_DANCE:  return "DANCE";
    case FID_LISTEN: return "LISTEN";
    case FID_SCARED: return "SCARED";
    case 0: return "Happy";     case 1: return "Sad";    case 2: return "Angry";
    case 3: return "Surprised"; case 4: return "Sleepy"; case 5: return "Love";
    case 6: return "Normal";
    default: return "?";
  }
}

void drawFaceNow() {
  if (eyeDemoActive) return;
  // ── Screen event log ──────────────────────────────────────────
  // Record every change of what is on the panel, so the Device Log (copyable
  // from the web UI) shows exactly what the firmware last tried to draw. If the
  // screen goes blank, the final "screen:" line names what it was painting when
  // it died — paste that log back to debug it. Deduped on the face id so dance
  // and per-tick animations (which re-enter drawFaceNow every frame) log ONCE
  // per transition instead of flooding the ring.
  uint8_t drawingId = danceMode        ? FID_DANCE
                    : listenActive()   ? FID_LISTEN
                    : scaredMode       ? FID_SCARED
                    :                    expression;
  static uint8_t lastLoggedId = 254;
  if (drawingId != lastLoggedId) {
    lastLoggedId = drawingId;
    dlogPrintf("screen: draw %s (expr=%d heap=%u)",
               faceName(drawingId), expression, ESP.getFreeHeap());
  }

  // Dance and listen do their own full-screen clear internally.
  if (danceMode)        { drawDanceFace(); lastFaceId = FID_DANCE;  return; }
  if (listenActive())   { tft.fillScreen(C_BG); drawListenFace(); drawBars(); lastFaceId = FID_LISTEN; return; }
  if (scaredMode)       { tft.fillScreen(C_BG); drawScaredFace(); drawBars(); lastFaceId = FID_SCARED; return; }

  // Coming in from a different face → wipe the whole screen once so nothing the
  // previous face drew outside the zone-erases (floating hearts, Zzz, smoke,
  // the listen mic + wave arcs, the dance rainbow) survives into this one.
  if (lastFaceId != expression) {
    tft.fillScreen(C_BG);   // full wipe on a real face switch
    // fall through: the per-zone erases below are now redundant-but-harmless
    // (they paint white onto white), and the switch() draws the new face.
  }
  lastFaceId = expression;

  // For all normal expressions: erase the three face zones individually so
  // no large white sweep is ever visible — each erase is tiny and immediate.
  // Eye drawing functions already erase their own bounding boxes, so we only
  // need to clear the brow strip and the mouth strip here.
  eraseBrows();   // clears FACE_TOP .. EYE_Y-EYE_R
  eraseMouth();   // clears EYE_Y+EYE_R+6 .. FACE_BOT

  // Also clear the narrow mid-face strip between the eyes and mouth that
  // none of the element functions touch (blush lives here on some expressions).
  int midTop = EYE_Y + EYE_R + 2;
  int midBot = EYE_Y + EYE_R + 6;
  tft.fillRect(0, midTop, SCR_W, midBot - midTop, C_BG);

  switch (expression) {
    case 0:  // Happy
      drawEye(EYE_LX, EYE_Y, false, gazeOX);
      drawEye(EYE_RX, EYE_Y, false, gazeOX);
      drawBrowFlat(EYE_LX, EYE_Y-EYE_R-10);
      drawBrowFlat(EYE_RX, EYE_Y-EYE_R-10);
      drawBlush();
      drawSmileHappy();
      if (!sparkInit) initSparks();
      break;

    case 1:  // Sad
      drawEye(EYE_LX, EYE_Y, false, 0);
      drawEye(EYE_RX, EYE_Y, false, 0);
      drawBrowSadL(EYE_LX, EYE_Y-EYE_R-12);
      drawBrowSadR(EYE_RX, EYE_Y-EYE_R-12);
      drawFrown();
      if (!tearActive) initTears();
      break;

    case 2:  // Angry
      drawAngryEye(EYE_LX, EYE_Y);
      drawAngryEye(EYE_RX, EYE_Y);
      drawBrowAngryL(EYE_LX, EYE_Y-EYE_R-8);
      drawBrowAngryR(EYE_RX, EYE_Y-EYE_R-8);
      // furrow crease between the pinched brows — sells the scowl
      tft.drawFastVLine(MOUTH_CX-2, EYE_Y-EYE_R-4, 9, C_BROW);
      tft.drawFastVLine(MOUTH_CX+2, EYE_Y-EYE_R-4, 9, C_BROW);
      drawMouthAngry();
      if (!fireInit) initFire();
      break;

    case 3:  // Surprised
      drawSurprisedEye(EYE_LX, EYE_Y);
      drawSurprisedEye(EYE_RX, EYE_Y);
      browOffset = 0; browDir = -1;
      drawMouthO();
      break;

    case 4:  // Sleepy
      drawSleepyEye(EYE_LX, EYE_Y);
      drawSleepyEye(EYE_RX, EYE_Y);
      for (int t=0; t<=2; t++) {
        tft.drawFastHLine(EYE_LX-12, EYE_Y-EYE_R-4+t, 24, C_BROW);
        tft.drawFastHLine(EYE_RX-12, EYE_Y-EYE_R-4+t, 24, C_BROW);
      }
      yawnOpen = 0; yawnDir = 1;
      drawSmileSmall();
      if (!zzzInit) initZzz();
      break;

    case 5:  // Love
      drawHeartEye(EYE_LX, EYE_Y);
      drawHeartEye(EYE_RX, EYE_Y);
      drawBrowFlat(EYE_LX, EYE_Y-EYE_R-10);
      drawBrowFlat(EYE_RX, EYE_Y-EYE_R-10);
      drawBlush();
      drawSmileLove();
      if (!heartFloatInit) initHearts();
      break;

    case 6:  // Normal
      drawEye(EYE_LX, EYE_Y, false, gazeOX);
      drawEye(EYE_RX, EYE_Y, false, gazeOX);
      drawBrowFlat(EYE_LX, EYE_Y-EYE_R-10);
      drawBrowFlat(EYE_RX, EYE_Y-EYE_R-10);
      drawMouthNormal();
      break;
  }

  drawBars();
}

// =============================================================
//  Blink — sweeps white lid, redraws eyes only (no mouth flicker)
// =============================================================
void blinkEyes() {
  if (danceMode) return;
  int r = EYE_R + 3;
  for (int i = 1; i <= BLINK_STEPS; i++) {
    int h = (r*2*i)/BLINK_STEPS;
    if (!winkL) tft.fillRect(EYE_LX-r, EYE_Y-r, r*2+1, h, C_BG);
    if (!winkR) tft.fillRect(EYE_RX-r, EYE_Y-r, r*2+1, h, C_BG);
    delay(BLINK_MS);
  }
  delay(60);
  eraseBrows();
  switch (expression) {
    case 0:
      drawEye(EYE_LX, EYE_Y, winkL, gazeOX);
      drawEye(EYE_RX, EYE_Y, winkR, gazeOX);
      drawBrowFlat(EYE_LX, EYE_Y-EYE_R-10);
      drawBrowFlat(EYE_RX, EYE_Y-EYE_R-10);
      break;
    case 1:
      drawEye(EYE_LX, EYE_Y, false, 0);
      drawEye(EYE_RX, EYE_Y, false, 0);
      drawBrowSadL(EYE_LX, EYE_Y-EYE_R-12);
      drawBrowSadR(EYE_RX, EYE_Y-EYE_R-12);
      break;
    case 2:
      drawAngryEye(EYE_LX, EYE_Y);
      drawAngryEye(EYE_RX, EYE_Y);
      drawBrowAngryL(EYE_LX, EYE_Y-EYE_R-8);
      drawBrowAngryR(EYE_RX, EYE_Y-EYE_R-8);
      break;
    case 3:
      drawSurprisedEye(EYE_LX, EYE_Y);
      drawSurprisedEye(EYE_RX, EYE_Y);
      break;
    case 4:
      drawSleepyEye(EYE_LX, EYE_Y);
      drawSleepyEye(EYE_RX, EYE_Y);
      for (int t=0; t<=2; t++) {
        tft.drawFastHLine(EYE_LX-12, EYE_Y-EYE_R-4+t, 24, C_BROW);
        tft.drawFastHLine(EYE_RX-12, EYE_Y-EYE_R-4+t, 24, C_BROW);
      }
      break;
    case 5:
      drawHeartEye(EYE_LX, EYE_Y);
      drawHeartEye(EYE_RX, EYE_Y);
      drawBrowFlat(EYE_LX, EYE_Y-EYE_R-10);
      drawBrowFlat(EYE_RX, EYE_Y-EYE_R-10);
      break;
    case 6:
      drawEye(EYE_LX, EYE_Y, false, gazeOX);
      drawEye(EYE_RX, EYE_Y, false, gazeOX);
      drawBrowFlat(EYE_LX, EYE_Y-EYE_R-10);
      drawBrowFlat(EYE_RX, EYE_Y-EYE_R-10);
      break;
  }
}

// =============================================================
//  Idle animation tick — 80ms, expression-specific
// =============================================================
// Forward decl
void redrawBobEyes_happy();

void exprAnimTick() {
  if (rpsHold)  return;   // RPS sequence owns the screen — do not touch it
  if (danceMode) {
    drawDanceFace();   // colour-cycle the dance face every 80ms tick
    return;
  }
  if (listenActive()) { tickListen(); return; }   // pulse the sound waves
  animFrame++;
  switch (expression) {
    case 0:  // Happy — sparkles + look/wink
      tickSparks();
      // look/wink idle (same as before)
      bobCounter++;
      switch (bobState) {
        case 0:
          if (bobCounter >= 38) {
            bobCounter = 0;
            static uint8_t trick = 0; trick = (trick+1)%4;
            bobState = trick+1;
          } break;
        case 1: if(bobCounter==1){gazeOX=-8;redrawBobEyes_happy();}
                if(bobCounter>=18){gazeOX=0;redrawBobEyes_happy();bobState=0;bobCounter=0;} break;
        case 2: if(bobCounter==1){gazeOX=+8;redrawBobEyes_happy();}
                if(bobCounter>=18){gazeOX=0;redrawBobEyes_happy();bobState=0;bobCounter=0;} break;
        case 3: if(bobCounter==1){winkR=true;redrawBobEyes_happy();}
                if(bobCounter>=14){winkR=false;redrawBobEyes_happy();bobState=0;bobCounter=0;} break;
        case 4: if(bobCounter==1){winkL=true;redrawBobEyes_happy();}
                if(bobCounter>=14){winkL=false;redrawBobEyes_happy();bobState=0;bobCounter=0;} break;
      }
      break;

    case 1:  // Sad — tears slide
      tickTears();
      break;

    case 2:  // Angry — fire + smoke
      tickFire();
      break;

    case 3:  // Surprised — brow bounce
      tickSurprisedBrows();
      break;

    case 4:  // Sleepy — yawn + Zzz
      tickSleepy();
      break;

    case 5:  // Love — floating hearts
      tickHearts();
      break;

    case 6:  // Normal — look idle (no extra fx)
      bobCounter++;
      switch (bobState) {
        case 0: if(bobCounter>=45){bobCounter=0;static uint8_t ntrick=0;ntrick=(ntrick+1)%2;bobState=ntrick+1;} break;
        case 1: if(bobCounter==1){gazeOX=-7;drawEye(EYE_LX,EYE_Y,false,gazeOX);drawEye(EYE_RX,EYE_Y,false,gazeOX);}
                if(bobCounter>=20){gazeOX=0;drawEye(EYE_LX,EYE_Y,false,0);drawEye(EYE_RX,EYE_Y,false,0);bobState=0;bobCounter=0;} break;
        case 2: if(bobCounter==1){gazeOX=+7;drawEye(EYE_LX,EYE_Y,false,gazeOX);drawEye(EYE_RX,EYE_Y,false,gazeOX);}
                if(bobCounter>=20){gazeOX=0;drawEye(EYE_LX,EYE_Y,false,0);drawEye(EYE_RX,EYE_Y,false,0);bobState=0;bobCounter=0;} break;
      }
      break;
  }
}

void redrawBobEyes_happy() {
  eraseBrows();
  tft.fillRect(EYE_LX-EYE_R-3, EYE_Y-EYE_R-3, (EYE_R+3)*2+1, (EYE_R+3)*2+1, C_BG);
  tft.fillRect(EYE_RX-EYE_R-3, EYE_Y-EYE_R-3, (EYE_R+3)*2+1, (EYE_R+3)*2+1, C_BG);
  drawEye(EYE_LX, EYE_Y, winkL, gazeOX);
  drawEye(EYE_RX, EYE_Y, winkR, gazeOX);
  drawBrowFlat(EYE_LX, EYE_Y-EYE_R-10);
  drawBrowFlat(EYE_RX, EYE_Y-EYE_R-10);
  drawBlush();
}

// =============================================================
//  Boot: Linux-style green terminal, followed by BOB wordmark
// =============================================================
// Yield between animation steps so other RTOS tasks can run.
static inline void tDelay(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}

void bootTypingIntro() {
  tft.fillScreen(C_BLACK);
  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setTextColor(C_GREEN, C_BLACK);
  const char* lines[] = {
    "ANVYXO OS / ESP32-S3",
    "[boot] starting system",
    "[ OK ] display init",
    "[load] face engine",
    "[wait] network setup"
  };
  for (int i = 0; i < 5; ++i) {
    tft.setCursor(4, 4 + i * 12);
    for (const char* c = lines[i]; *c; ++c) {
      tft.print(*c);
      tDelay(10);
    }
    tDelay(100);
  }
  // ANVYXO wordmark, centred for its length (6 px base glyph * size 3 = 18 px/char).
  tft.setTextSize(3);
  const char* mark = "ANVYXO";
  const int   markLen  = 6;
  const int   charAdv  = 18;
  const int   markX    = (SCR_W - markLen * charAdv) / 2;
  for (int i = 0; i < markLen; ++i) {
    tft.setCursor(markX + i * charAdv, 72);
    tft.print(mark[i]);
    tDelay(140);
  }
  tft.setTextSize(1);
  tft.setCursor(4, 112);
  tft.print("root@anvyxo:~# wifi connect");
  // Leave the terminal visible during the connection attempt.
}

// =============================================================
//  screenSetup / screenTick
// =============================================================
void screenSetup() {
#if TFT_HW_SPI
  // Initialize the bus with this profile's pins before Adafruit calls
  // SPI.begin(). No MISO or default SS pin should be claimed.
  SPI.begin(TFT_CLK, -1, TFT_MOSI, -1);
#endif
  tft.initR(INITR_BLACKTAB);
  // Slow the SPI clock down from the library default of 32 MHz. That default is
  // far above what an ST7735 reliably clocks over dupont/jumper wiring, and with
  // the servo PWM lines running alongside a MISO-less bus (no read-back, so a
  // corrupted transfer is undetectable) 32 MHz is what turns noise into random
  // white/black patches on the panel. 8 MHz is well within the ST7735's spec and
  // dramatically more noise-immune; a full-screen paint still finishes in ~40 ms,
  // imperceptible for this UI. This is the root-cause fix — no repainting tricks.
  // If ANY white flecks remain, drop this to 4000000 (4 MHz).
  tft.setSPISpeed(8000000);
  tft.setRotation(1);
  bootTypingIntro();
  nextBlink = millis() + random(BLINK_MIN, BLINK_MAX);
  nextAnim  = millis() + 80;
}

// Scripted liquid eyes: no servo writes, no calibration or IMU setting changes.
// A small offscreen canvas prevents erase/draw flicker on the TFT.
void stopEyeDemo() {
  eyeDemoActive = false;
  lastFaceId = 255; prevExpr = 255;
  drawFaceNow();
}

float eyeDemoEase(float u) {
  u=constrain(u,0.0f,1.0f);
  return u*u*u*(u*(u*6-15)+10);  // zero speed/acceleration at both ends
}
// Wider sweep (±22 px vs ±14) so the look-left / look-right reads clearly. The
// easing below is already a quintic smootherstep (zero speed + acceleration at
// both ends), so the motion stays buttery; EYE_SWEEP just makes it bigger.
#define EYE_SWEEP 22.0f
float eyeDemoSlide(float t) {
  if(t<2.4f) return -EYE_SWEEP*eyeDemoEase(t/2.4f);
  if(t<5.2f) return -EYE_SWEEP+2*EYE_SWEEP*eyeDemoEase((t-2.4f)/2.8f);
  if(t<6.4f) return EYE_SWEEP*(1-eyeDemoEase((t-5.2f)/1.2f));
  return 0;
}
void eyeDemoTick(unsigned long now) {
  if ((int32_t)(now - eyeDemoNext) < 0) return;
  eyeDemoNext = now + 16;  // ~60 fps for a smoother slide (was 30 fps)
  const unsigned long elapsed = now - eyeDemoStart;
  if (elapsed >= 11000) { stopEyeDemo(); return; }
  static GFXcanvas16 frame(SCR_W, 100);
  if (!frame.getBuffer()) { stopEyeDemo(); return; }
  float t=elapsed/1000.0f;
  float shake=0, bob=0;
  if(t>=6.4f && t<8.0f) {
    // Much more violent shake — bigger amplitude + faster rattle.
    float envelope=sinf((t-6.4f)/1.6f*PI);
    shake=13*sinf((t-6.4f)*22)*envelope;
    bob=6*sinf((t-6.4f)*17)*envelope;
  }
  float x=eyeDemoSlide(t)+shake;
  float mouthX=.55f*eyeDemoSlide(fmaxf(0,t-.18f))+.35f*shake;
  float anger=eyeDemoEase((t-8.0f)/.5f);
  frame.fillScreen(C_BG);
  for(int eye=0;eye<2;++eye) {
    int cx=(eye?EYE_RX:EYE_LX)+(int)lroundf(x);
    int cy=EYE_Y-14+(int)lroundf(bob+(eye?1:-1)*x*.08f);
    // Identical circles, colour and highlights to the normal face.
    drawEyeOn(frame,cx,cy,false,(int8_t)(x*.12f));
    if(anger>0) {
      int inner=cy-EYE_R+(int)(anger*17);
      if(!eye)frame.fillTriangle(cx-EYE_R,cy-EYE_R,cx+EYE_R,cy-EYE_R,cx+EYE_R,inner,C_BG);
      else frame.fillTriangle(cx-EYE_R,cy-EYE_R,cx+EYE_R,cy-EYE_R,cx-EYE_R,inner,C_BG);
    }
    int brow=cy-EYE_R-9;
    for(int k=0;k<3;++k) {
      int tilt=(int)(anger*8);
      frame.drawLine(cx-14,brow+k+(eye?tilt:0),cx+14,brow+k+(eye?0:tilt),C_BROW);
    }
  }
  int mx=MOUTH_CX+(int)lroundf(mouthX),my=MOUTH_Y-14;
  if(t>=8.5f) drawMouthAngryOn(frame,mx,my-4);
  else if(t>=6.4f && t<8.0f) {
    int radius=6+(int)lroundf(fabsf(shake)*.3f);
    frame.fillCircle(mx,my-4,radius,C_MOUTH);
    frame.fillCircle(mx,my-4,radius-3,C_BG);
  } else drawMouthNormalOn(frame,mx,my-6,x*.009f);
  tft.drawRGBBitmap(0,14,frame.getBuffer(),SCR_W,100);
  // No directional captions: keep all attention on the face. Only the final
  // angry comeback occupies the existing bottom bar, clear of the mouth.
  if(t>=8.5f) {
    tft.fillRect(0,BOT_BAR_Y,SCR_W,BAR_H,C_BG);
    tft.setTextSize(1);tft.setTextColor(C_EYE,C_BG);
    tft.setCursor(35,BOT_BAR_Y+3);tft.print("HEY! Easy on me!");
  }
}

void handleEyeDemo() {
  if(server.hasArg("stop")) { stopEyeDemo(); server.send(200,"text/plain","Eyes demo stopped"); return; }
  if(scaredMode || danceMode) { server.send(409,"text/plain","Wait for the current reaction to finish"); return; }
  rpsHold=false;listenUntil=0;netScreenUntil=0;
  tickerCount=0;tickerUntil=0;tickerMsg="";
  eyeDemoActive=true;eyeDemoStart=millis();eyeDemoNext=eyeDemoStart;
  tft.fillScreen(C_BG);drawBars();
  server.send(200,"text/plain","Playing 11 s screen demo");
}

void screenTick() {
  unsigned long now = millis();
  if (eyeDemoActive) { eyeDemoTick(now); return; }
  if (netScreenUntil) {
    if ((int32_t)(now - netScreenUntil) < 0) return;
    netScreenUntil = 0;
    lastFaceId = 255;
    prevExpr = 255;
    drawFaceNow();
  }

  // RPS sequence is painting the screen itself (countdown or reveal) — do not
  // touch the display at all until /eyes ends the round and clears rpsHold.
  if (rpsHold) {
    tickerTick();   // keep the bottom bar ticker ticking
    return;
  }

  // After a servo move, force ONE full face redraw to clear any SPI noise
  // artefacts. handleMove() already let the bus settle before setting the flag,
  // so this single on-demand repaint lands on a quiet line. No repeated timer,
  // no extra display commands — repainting more often than this just gives the
  // noise more frames to corrupt.
  if (needFullRedraw) {
    needFullRedraw = false;   // consume immediately so it never fires twice
    prevExpr = 255;           // triggers the drawFaceNow() path below
  }

  // Status bars first, ahead of every early return below, so the marquee
  // keeps scrolling and the WiFi icon stays honest in all expressions.
  tickerTick();
  if ((WiFi.status() == WL_CONNECTED) != lastWifiUp) drawTopBar();

  // Mic went live / went quiet → swap the whole face over
  bool listening = listenActive();
  if (listening != prevListen) {
    prevListen  = listening;
    listenFrame = 0;
    if (listening) { drawFaceNow(); nextAnim = now + 80; }
    else           { prevExpr = 255; }   // next tick rebuilds the normal face
    return;
  }

  // Expression or dance changed → full redraw + reset particles
  if (expression != prevExpr || danceMode) {
    if (!danceMode) {
      bobState=0; bobCounter=0; gazeOX=0;
      winkL=false; winkR=false; animFrame=0;
      // reset particle flags so they reinit on new expression
      tearActive=false; fireInit=false; zzzInit=false;
      sparkInit=false; heartFloatInit=false;
      browOffset=0; browDir=-1;
      yawnOpen=0; yawnDir=1;
    }
    drawFaceNow();
    prevExpr  = expression;
    nextBlink = now + random(BLINK_MIN, BLINK_MAX);
    nextAnim  = now + 80;
    return;
  }

  // Scared: roaming pupils + flowing sweat every 80 ms — but ONLY once the feet
  // have stopped slewing. While feetFlipping is set the panel is left untouched
  // (the static scared face painted by startLiftReaction stays up), so no SPI
  // write ever coincides with a servo switch. That is the whole fix for the
  // "scared → rotates / goes white" corruption; the animation picks back up for
  // the settle/recal tail of the reaction.
  if (scaredMode) {
    if (!feetFlipping && now >= nextAnim) {
      scaredAnimTick();
      nextAnim = now + 80;
    }
    return;
  }

  // Auto-blink (skip during dance/listening, skip for angry/surprised — busy)
  if (!danceMode && !listening && now >= nextBlink
      && expression != 2 && expression != 3) {
    blinkEyes();
    nextBlink = now + random(BLINK_MIN, BLINK_MAX);
    return;
  }

  // Animation tick
  if (now >= nextAnim) {
    exprAnimTick();
    nextAnim = now + 80;
  }
}

// =============================================================
//  Embedded HTML UI
// =============================================================
// Gzip-precompressed UI pages (generated from html_page.h / motion_page.h by
// tools/gen_web_gz.py). Serving the compressed bytes cuts the main page from
// ~120 KB to ~28 KB over the slow SoftAP. Re-run the generator after editing
// the source .h pages.
#include "web_gz.h"

// =============================================================
//  Forward declarations — variables and functions defined later
//  in the file but used by HTTP handlers above their definition.
// =============================================================
extern bool   fsReady;
extern bool   isRecording;
extern bool   imuReady;
extern bool   imuCalValid;
extern int    imuLiftDelta;
extern long   imuRestMag;
int  maxRecordSecs();
bool recordToFile(const String& path, int secs, String& err);
void playWavFile(const String& path);

// =============================================================
//  HTTP handlers
// =============================================================
// Serve a gzip-compressed PROGMEM page with long-lived caching so reloads are
// instant and the first load transfers ~4x fewer bytes over the SoftAP.
static void sendGzipPage(const unsigned char* data, unsigned int len) {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "public, max-age=86400");
  server.send_P(200, "text/html", (const char*)data, len);
}

void handleRoot()  { sendGzipPage(HTML_PAGE_GZ, HTML_PAGE_GZ_LEN); }

void handleState() {
  String j = "{\"trim\":[";
  j+=trim[0];j+=",";j+=trim[1];j+=",";j+=trim[2];j+=",";j+=trim[3];
  j+="],\"speed\":";j+=stepPeriod;j+=",\"swing\":";j+=hipSwing;
  j+=",\"tilt\":";j+=footTilt;j+=",\"eye\":";j+=expression;
  j+=",\"mch\":";j+=micCh;j+=",\"mfmt\":";j+=micFmt;
  j+=",\"mfix\":";j+=micFix;j+=",\"msh\":";j+=micShift;
  j+=",\"imu\":";j+=(imuReady?1:0);
  // scaredMode + reactState — polled by BOB_ROBO_CONVO idle loop to detect
  // pick-up / shake events and play a TTS reaction on the Mac.
  j+=",\"scaredMode\":"; j+=(scaredMode ? 1 : 0);
  j+=",\"react\":";      j+=(int)reactState;
  // roll/pitch (degrees, zeroed to the calibrated pose) so the idle loop can
  // detect left/right TILT from this one poll — no separate /imupose request.
  // 0 when uncalibrated so the Mac never reacts to a bogus angle.
  {
    float p = 0.0f, r = 0.0f;
    if (imuCalValid) {
      float rawP, rawR;
      accelPitchRoll(imu_ax, imu_ay, imu_az, &rawP, &rawR);
      p = angleDelta(rawP, imuCalPitch);
      r = angleDelta(rawR, imuCalRoll);
    }
    j+=",\"roll\":";  j+=String(r, 1);
    j+=",\"pitch\":"; j+=String(p, 1);
  }
#if USE_SAM_TTS
  j+=",\"sam\":1";
#else
  j+=",\"sam\":0";
#endif
  j+="}";
  server.send(200, "application/json", j);
}

// GET /eyes?m=0..6  (expression select)
void handleEyes() {
  if (!server.hasArg("m")) { server.send(400,"text/plain","missing m"); return; }
  expression = constrain(server.arg("m").toInt(), 0, 6);
  // Setting an expression also ends any frozen RPS result on screen. The RPS
  // reveal painted the WHOLE panel (boulder/paper/scissors art), so the next
  // face must do a full fillScreen wipe — otherwise, if the round ends on the
  // same expression it started with, drawFaceNow()'s "same face → zone-erase
  // only" fast path leaves the RPS art bleeding through behind the new face.
  // Invalidating lastFaceId forces that full wipe.
  if (rpsHold) { rpsHold = false; danceMode = false; lastFaceId = 255; }
  prevExpr   = 255;   // force redraw
  server.send(200, "text/plain", "ok");
}

// GET /ticker?text=Hello — show a message on the bottom bar, standing still.
// Up to 26 chars is one page and holds for TICKER_SECS. Longer text is split
// into pages that each hold 1.5 s, so the whole sentence gets read once.
// Returns immediately: the page flipping is done by tickerTick() from
// screenTick(), so the web server is never blocked while it plays.
void handleTicker() {
  String t = server.arg("text");
  t.trim();
  if (t.length() == 0) { server.send(400, "text/plain", "missing text"); return; }
  const int cap = TICKER_MAX_PAGES * (TICKER_CHARS + 1);   // 108 chars
  if ((int)t.length() > cap) t = t.substring(0, cap);
  tickerMsg = t;
  tickerSplit(t);
  tickerPage  = 0;
  unsigned long now = millis();
  // One page: the old 5 s. Several: exactly one pass, 1.5 s each.
  tickerUntil = now + (tickerCount > 1 ? (unsigned long)tickerCount * TICKER_PAGE_MS
                                       : (unsigned long)TICKER_SECS * 1000UL);
  nextTicker  = now + TICKER_PAGE_MS;
  drawTickerText();                       // show page 1 immediately
  dlogPrintf("ticker: \"%s\" in %u page(s)", t.c_str(), tickerCount);
  server.send(200, "text/plain", "ok");
}

// =============================================================
//  /screen/<idle|listen|speak>
//  Called by BOB_ROBO_CONVO Python server to switch the TFT face
//  context — idle = normal animated face, listen = mic ear, speak = speaking.
//  These are fire-and-forget: Python doesn't wait for the animation to finish.
// =============================================================
void handleScreen() {
  // URL is /screen/idle  /screen/listen  /screen/speak
  String uri  = server.uri();           // e.g. "/screen/listen"
  int    sl   = uri.lastIndexOf('/');
  String mode = (sl >= 0) ? uri.substring(sl + 1) : "";
  mode.toLowerCase();

  if (mode == "listen") {
    // Show the listening ear for up to 12 s (Python will call again each turn)
    listenUntil = millis() + 12000UL;
    dlogPrintf("screen: listen mode (listenUntil+12s)");
  } else if (mode == "speak") {
    // Dismiss the listen ear; let screenTick() show the current expression
    listenUntil = 0;
    dlogPrintf("screen: speak mode");
  } else {
    // "idle" or anything else — dismiss listen ear, return to normal face
    listenUntil = 0;
    dlogPrintf("screen: idle mode");
  }
  server.send(200, "text/plain", "ok");
}

// =============================================================
//  Rock Paper Scissors game
//  Countdown 1 → 2 → 3 on full screen, then show random result.
//  Runs synchronously (blocking), returns to normal face via
//  prevExpr=255 flag so screenTick() repaints cleanly.
// =============================================================

// Draw a single large countdown digit centred on screen.
static void rpsDrawCountdown(uint8_t n) {
  tft.fillScreen(C_BG);
  // Large digit: textSize 8 = 8×(6×8) = 48×64 px per char, centred on 160×128
  tft.setTextSize(8);
  tft.setTextColor(C_EYE);
  // Each char cell at size 8 is 6*8=48 wide, 8*8=64 tall
  tft.setCursor((SCR_W - 48) / 2, (SCR_H - 64) / 2);
  tft.print(n);
  tft.setTextSize(1);   // restore default
}

// ── Rock: smooth glossy boulder (clean bold silhouette) ───────
// Built from overlapping circles for an organic rounded mass with a
// flat base, a grey drop-shadow, and white gloss highlights.
static void rpsRockAt(int cx, int cy) {
  // Grey drop-shadow ellipse on the "ground"
  tft.fillCircle(cx + 6, cy + 30, 24, 0xC618);
  tft.fillCircle(cx - 10, cy + 32, 14, 0xC618);

  // Main boulder mass — several overlapping black circles
  tft.fillCircle(cx,        cy,      30, C_EYE);
  tft.fillCircle(cx - 20,   cy + 8,  17, C_EYE);
  tft.fillCircle(cx + 20,   cy + 8,  17, C_EYE);
  tft.fillCircle(cx,        cy + 12, 24, C_EYE);

  // Flatten the bottom so it "sits" on the ground
  tft.fillRect(cx - 40, cy + 26, 80, 20, C_BG);
  tft.fillRoundRect(cx - 34, cy + 12, 68, 16, 7, C_EYE);

  // White gloss highlights — top-left shine
  tft.fillCircle(cx - 13, cy - 12, 5, C_BG);
  tft.fillCircle(cx - 4,  cy - 15, 2, C_BG);

  // Subtle facet cracks
  tft.drawLine(cx + 8,  cy - 18, cx + 3,  cy + 6,  C_BG);
  tft.drawLine(cx + 3,  cy + 6,  cx + 16, cy + 10, C_BG);
}

static void rpsDrawRock() {
  tft.fillScreen(C_BG);
  tft.setTextSize(2); tft.setTextColor(C_EYE);
  tft.setCursor(SCR_W/2 - 30, 6); tft.print("ROCK");
  tft.setTextSize(1);
  // Animate: 3 quick "bounce" frames — rock drops in from above
  for (int f = 0; f < 3; f++) {
    int drop = (2 - f) * 8;   // 16 → 8 → 0 px above final position
    tft.fillRect(0, 26, SCR_W, SCR_H - 26, C_BG);
    rpsRockAt(SCR_W / 2, 72 - drop);
    delay(140);
  }
}

// ── Paper: clean rounded sheet with a big folded corner + lines ─
static void rpsPaperAt(int cx, int cy) {
  int w = 60, h = 74;
  int x0 = cx - w/2, y0 = cy - h/2;
  int fc = 20;   // folded-corner size (bigger, more obvious)

  // Grey drop-shadow, offset down-right
  tft.fillRoundRect(x0 + 5, y0 + 5, w, h, 6, 0xC618);

  // Sheet body — black silhouette with the top-right corner cut off
  tft.fillRoundRect(x0, y0, w, h, 6, C_EYE);
  tft.fillTriangle(x0 + w - fc, y0, x0 + w + 1, y0, x0 + w + 1, y0 + fc, C_BG);

  // The folded-over corner flap (grey wedge)
  tft.fillTriangle(x0 + w - fc, y0, x0 + w - fc, y0 + fc, x0 + w, y0 + fc, 0xC618);
  tft.drawLine(x0 + w - fc, y0, x0 + w, y0 + fc, C_EYE);

  // White ruled text lines
  for (int i = 0; i < 5; i++)
    tft.fillRect(x0 + 9, y0 + 20 + i * 10, w - 20, 2, C_BG);

  // Blue left margin
  tft.drawFastVLine(x0 + 13, y0 + 14, h - 22, 0x001F);
}

static void rpsDrawPaper() {
  tft.fillScreen(C_BG);
  tft.setTextSize(2); tft.setTextColor(C_EYE);
  tft.setCursor(SCR_W/2 - 36, 6); tft.print("PAPER");
  tft.setTextSize(1);
  // Animate: paper flutters in — 3 slight rotation-sim frames (vertical squish)
  for (int f = 0; f < 3; f++) {
    tft.fillRect(0, 26, SCR_W, SCR_H - 26, C_BG);
    int squish = (2 - f) * 5;   // taller → normal (perspective flutter)
    int cx = SCR_W / 2, cy = 76;
    int w = 58, h = 70 - squish;
    int x0 = cx - w/2, y0 = cy - h/2;
    tft.fillRect(x0 + 4, y0 + 4, w, h, 0xC618);
    tft.fillRect(x0, y0, w, h, C_EYE);
    int fc = 14;
    tft.fillTriangle(x0+w-fc,y0, x0+w,y0, x0+w,y0+fc, C_BG);
    tft.drawLine(x0+w-fc,y0, x0+w,y0+fc, 0xC618);
    for (int i = 1; i <= 5; i++)
      tft.drawFastHLine(x0+6, y0+10+i*9, w-18, C_BG);
    tft.drawFastVLine(x0+12, y0+10, h-16, 0x001F);
    tft.drawRect(x0, y0, w, h, 0xC618);
    delay(140);
  }
}

// ── Scissors: bold filled blades in a V, chunky handle rings ──
static void rpsScissorsAt(int cx, int cy, int openPx) {
  // openPx: how far apart the blade tips are (0 = closed, 28 = open)
  int tipLx = cx - openPx, tipLy = cy - 34;
  int tipRx = cx + openPx, tipRy = cy - 34;
  int pivx  = cx,          pivy  = cy;        // pivot

  // Blades as filled triangles (bold, readable) tapering to the tips
  tft.fillTriangle(tipLx - 5, tipLy, tipLx + 5, tipLy + 3, pivx, pivy, C_EYE);  // left blade
  tft.fillTriangle(tipRx + 5, tipRy, tipRx - 5, tipRy + 3, pivx, pivy, C_EYE);  // right blade

  // White inner slivers so the two blades read as separate metal
  tft.drawLine(tipLx, tipLy + 3, pivx - 1, pivy - 3, C_BG);
  tft.drawLine(tipRx, tipRy + 3, pivx + 1, pivy - 3, C_BG);

  // Handle shanks (thick) down to the finger rings
  for (int t = -2; t <= 2; t++) {
    tft.drawLine(pivx, pivy, cx - 15 + t, cy + 30, C_EYE);
    tft.drawLine(pivx, pivy, cx + 15 + t, cy + 30, C_EYE);
  }

  // Chunky finger rings (thick circles) at the handle ends
  for (int r = 12; r >= 9; r--) {
    tft.drawCircle(cx - 15, cy + 40, r, C_EYE);
    tft.drawCircle(cx + 15, cy + 40, r, C_EYE);
  }

  // Pivot screw
  tft.fillCircle(pivx, pivy, 6, C_EYE);
  tft.fillCircle(pivx, pivy, 3, C_BG);
}

static void rpsDrawScissors() {
  tft.fillScreen(C_BG);
  tft.setTextSize(2); tft.setTextColor(C_EYE);
  tft.setCursor(SCR_W/2 - 46, 6); tft.print("SCISSORS");
  tft.setTextSize(1);
  // Animate: scissors snap open — 3 frames from closed to open
  int opens[] = {0, 14, 26};
  for (int f = 0; f < 3; f++) {
    tft.fillRect(0, 26, SCR_W, SCR_H - 26, C_BG);
    rpsScissorsAt(SCR_W / 2, 64, opens[f]);
    delay(160);
  }
}

// Full game sequence: 1 → 2 → 3 → random result held for 3 s → face restored.
void rpsGame() {
  // Freeze screenTick() by flagging a fake scaredMode-equivalent: use danceMode
  // so no other code repaints during the sequence.
  bool savedDance = danceMode;
  danceMode = true;

  // Countdown
  rpsDrawCountdown(1); delay(1000);
  rpsDrawCountdown(2); delay(1000);
  rpsDrawCountdown(3); delay(1000);

  // Pick random result
  uint8_t pick = (uint8_t)(random(0, 3));   // 0=Rock 1=Paper 2=Scissors
  dlogPrintf("rps: result=%d", (int)pick);
  if      (pick == 0) rpsDrawRock();
  else if (pick == 1) rpsDrawPaper();
  else                rpsDrawScissors();

  // Hold result for 3 s
  delay(3000);

  // Restore normal face
  danceMode = savedDance;
  prevExpr  = 255;        // force screenTick() to repaint the real face
}

// GET /rps — trigger the self-contained game from the web UI (random, auto-restores)
void handleRps() {
  server.send(200, "text/plain", "ok");
  rpsGame();
}

// Draw a single static RPS result (no animation), with its label at the top.
// 0=Rock 1=Paper 2=Scissors
static void rpsShow(uint8_t pick) {
  tft.fillScreen(C_BG);
  tft.setTextSize(2); tft.setTextColor(C_EYE);
  const char* label = (pick == 0) ? "ROCK" : (pick == 1) ? "PAPER" : "SCISSORS";
  int len = strlen(label);
  tft.setCursor((SCR_W - len * 12) / 2, 6);   // size-2 char ≈ 12 px wide
  tft.print(label);
  tft.setTextSize(1);
  if      (pick == 0) rpsRockAt(SCR_W / 2, 74);
  else if (pick == 1) rpsPaperAt(SCR_W / 2, 78);
  else                rpsScissorsAt(SCR_W / 2, 66, 26);
}

// ── GET /rps/go ───────────────────────────────────────────────
// Python-driven game, step 1: play the 1-2-3 countdown and FREEZE on "3".
// The caller (Mac) snaps the ESP32-CAM the instant this returns, then calls
// /rps/reveal to show Bob's throw. Screen stays frozen (rpsHold=true)
// until the next /eyes call ends the round and clears rpsHold.
//
// NOTE: we do NOT set danceMode here. danceMode causes screenTick() to call
// drawDanceFace() every 80 ms, which would overwrite the countdown digits with
// the rainbow dance face. rpsHold alone is enough: screenTick() returns early
// when rpsHold is set, leaving whatever we just drew on the panel untouched.
void handleRpsGo() {
  rpsHold = true;
  rpsDrawCountdown(1); delay(800);
  rpsDrawCountdown(2); delay(800);
  rpsDrawCountdown(3); delay(800);
  dlogPrintf("rps: countdown done — frozen on 3");
  server.send(200, "text/plain", "ok");
}

// ── GET /rps/reveal?pick=rock|paper|scissors  (or 0|1|2) ──────
// Python-driven game, step 2: display Bob's chosen throw and hold it.
// rpsHold keeps screenTick() hands-off until /eyes clears it.
void handleRpsReveal() {
  String p = server.arg("pick");
  p.toLowerCase();
  uint8_t pick;
  if      (p == "rock"     || p == "0") pick = 0;
  else if (p == "paper"    || p == "1") pick = 1;
  else if (p == "scissors" || p == "2") pick = 2;
  else { server.send(400, "text/plain", "pick must be rock|paper|scissors"); return; }

  rpsHold = true;   // keep the result frozen until /eyes ends the round
  rpsShow(pick);
  dlogPrintf("rps: reveal=%s", p.c_str());

  const char* name = (pick == 0) ? "rock" : (pick == 1) ? "paper" : "scissors";
  String j = "{\"pick\":\""; j += name; j += "\"}";
  server.send(200, "application/json", j);
}

void handleMove() {
  if (!server.hasArg("cmd")) { server.send(400,"text/plain","missing cmd"); return; }
  String cmd = server.arg("cmd");
  if (motion.armed && (cmd=="stand" || cmd=="stop")) {
    motionDisarm("stopped from movement controls");
    imuCalValid=false;
    server.send(200,"text/plain","stopped; holding last position");
    return;
  }
  // Motion Studio owns the servos while armed; a legacy gait would fight its
  // control loop. Everything else (gaits, dances, IMU moves) now runs
  // home-relative around the saved calibrated pose, so custom idle is fine.
  if (motion.armed) {
    server.send(409,"text/plain","Motion Studio has an armed session. Stop it first (Stand/Stop)."); return;
  }
  server.send(200, "text/plain", "ok");
  dlogPrintf("move: %s (heap=%u)", cmd.c_str(), ESP.getFreeHeap());
  // Always begin a movement from the calibrated home pose so it never lurches
  // from wherever the legs were last left. Dances already glide home in
  // danceOne(); stand/stop go home themselves — skip the redundant move for them.
  if (cmd!="stand" && cmd!="stop" && !cmd.startsWith("dance")) glideNeutral();
  if      (cmd=="fwd")    walkForward(4);
  else if (cmd=="bwd")    walkBackward(4);
  else if (cmd=="left")   turnLeft(3);
  else if (cmd=="right")  turnRight(3);
  else if (cmd=="dance")  dance(8);          // auto — cycles through all 5
  else if (cmd=="dance0") danceOne(0, 8);
  else if (cmd=="dance1") danceOne(1, 8);
  else if (cmd=="dance2") danceOne(2, 8);
  else if (cmd=="dance3") danceOne(3, 8);
  else if (cmd=="dance4") danceOne(4, 8);
  else if (cmd=="dance5") danceHappy(2);
  else if (cmd=="dance6") danceGroove(2);
  else if (cmd=="stand")  standNeutral();
  // ── IMU-driven moves ─────────────────────────────────────────
  else if (cmd=="imu_lean")     moveLean(4000);
  else if (cmd=="imu_balance")  moveBalance(4000);
  else if (cmd=="imu_tiltwalk") moveTiltWalk(5);
  else if (cmd=="imu_swagger")  moveSwagger(6);
  else if (cmd=="imu_sway")     moveSway(6);
  // Servo PWM lines can couple noise into the SPI bus during blocking moves.
  // Let the bus settle briefly so the repaint below lands on a quiet line, then
  // flag a full face redraw so screenTick() repaints all face zones cleanly.
  // Do NOT call tft.initR() here — that resets the controller's GRAM to white
  // and is exactly what causes the white screen flash after every motor command.
  delay(30);
  needFullRedraw = true;
}

void handleTrim() {
  if (!server.hasArg("ch")||!server.hasArg("v")) { server.send(400,"text/plain","missing args"); return; }
  int ch = constrain(server.arg("ch").toInt(), 0, 3);
  trim[ch] = constrain(server.arg("v").toInt(), -45, 45);
  standNeutral();
  server.send(200, "text/plain", "ok");
}

void handleSave() { bool ok=saveTrims(); server.send(ok?200:500,"text/plain",ok?"ok":"Flash save failed"); }
void handleAssembly()  { setServoRaw90(); server.send(200,"text/plain","ok"); }

// GET /servopos?ch=N&deg=D — move one servo to an absolute position.
// ch: 0=Left Hip, 1=Left Foot, 2=Right Hip, 3=Right Foot
// deg: 0..180  (trim is applied as usual so the physical motion is centred on 90°)
// Also returns JSON with all 4 current positions so the UI can stay in sync.
void handleServoPos() {
  if (server.hasArg("ch") && server.hasArg("deg")) {
    int ch  = constrain(server.arg("ch").toInt(),  0, 3);
    int deg = constrain(server.arg("deg").toInt(), 0, 180);
    setServo(ch, deg);
    dlogPrintf("servopos: ch=%d deg=%d", ch, deg);
  }
  // Return all 4 current positions so the UI sliders can be initialised.
  String j = "{\"pos\":[";
  for (int i = 0; i < 4; i++) { if (i) j += ","; j += servoCur[i]; }
  j += "]}";
  server.send(200, "application/json", j);
}

// GET /servocalibrate — send all 4 motors to their natural centre (90°) WITH
// trim applied. This is the true neutral from which they move equal angles in
// both directions. After this call servoCur[i] == 90 for all i.
void handleServoCalibrate() {
  // Electrical horn alignment is distinct from the assembled idle pose.
  for (unsigned i=0;i<4;++i) setServo(i,90);
  dlogPrintf("servocalibrate: all 4 → 90° (trim applied)");
  server.send(200, "text/plain", "ok");
}

// GET /servotest?ch=N&us=U — write a raw microsecond pulse to one servo,
// bypassing degToUs() and trim entirely. Use this to confirm a servo is wired
// to the correct GPIO and responding. ch=0..3; pulses clamp to configured limits.
// Returns plain text with the channel, GPIO, and pulse sent.
// Examples:
//   /servotest?ch=0&us=1500   → neutral on Left Hip
//   /servotest?ch=0&us=1000   → ~45° one way
//   /servotest?ch=0&us=2000   → ~45° the other way
void handleServoTest() {
  int ch = server.hasArg("ch") ? constrain(server.arg("ch").toInt(), 0, 3) : 0;
  int us = server.hasArg("us") ? constrain(server.arg("us").toInt(), SERVO_MIN_US, SERVO_MAX_US) : 1500;
  static const char* const SERVO_NAMES[4] = {"LH","LF","RH","RF"};

  bool attached = (servos[ch].pin >= 0);
  bool written = attached && servos[ch].writeMicroseconds(us);
  if (written) {
    servoCur[ch] = map(us, SERVO_MIN_US, SERVO_MAX_US, 0, 180)-trim[ch];
  }

  char buf[384];
  snprintf(buf, sizeof(buf),
    "servotest ch=%d (%s GPIO%d) us=%d  attach=%s  MIN_US=%d MAX_US=%d\r\n"
    "%s",
    ch, SERVO_NAMES[ch], servoPins[ch], us,
    attached ? "OK" : "FAIL (GPIO not attached — check wiring and reflash)",
    SERVO_MIN_US, SERVO_MAX_US,
    written ? "PWM accepted (not motion feedback). Check external servo power, common GND and signal GPIO."
            : "PWM failed: inspect startup logs and pin assignment.");
  dlogPrintf("servotest: ch=%d us=%d attach=%s", ch, us, attached?"ok":"FAIL");
  server.send(written ? 200 : 503, "text/plain", buf);
}

void handleResetTrim() {
  int previous[4];
  for (int i=0;i<4;i++) { previous[i]=trim[i]; trim[i]=0; }
  if (!saveTrims()) {
    for (int i=0;i<4;i++) trim[i]=previous[i];
    server.send(500,"text/plain","Flash save failed"); return;
  }
  standNeutral();
  server.send(200,"text/plain","ok");
}

void handleParam() {
  if (server.hasArg("speed")) stepPeriod = constrain(server.arg("speed").toInt(),200,1200);
  if (server.hasArg("swing")) hipSwing   = constrain(server.arg("swing").toInt(),5,45);
  if (server.hasArg("tilt"))  footTilt   = constrain(server.arg("tilt").toInt(),5,40);
  server.send(200,"text/plain","ok");
}

// GET /record — capture a short WAV. Visual bring-up keeps clip flash disabled,
// so quick recording uses a small RAM buffer in this profile.
// Optional query params: secs=N, silence_ms=N
void handleRecord() {
  if (isRecording) { server.send(503,"text/plain","busy"); return; }

  int secs = server.hasArg("secs")
    ? constrain(server.arg("secs").toInt(), 1, BOB_VISUAL_BRINGUP ? RAM_RECORD_SECS : maxRecordSecs())
    : constrain(RECORD_SECS, 1, BOB_VISUAL_BRINGUP ? RAM_RECORD_SECS : maxRecordSecs());
  dlogPrintf("record: start %d s (heap=%u free, %u largest)",
             secs, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Paint the ear face now — recordToFile() blocks for `secs` seconds
  listenUntil = millis() + (unsigned long)(secs + 2) * 1000UL;
  prevListen  = true;
  drawFaceNow();

  if (BOB_VISUAL_BRINGUP) {
    recordMic(secs);
    if (!wavBuf || wavBufLen == 0) {
      server.send(500, "text/plain", "mic returned no audio (check /hw and /logs)");
      return;
    }
    uint8_t hdr[44];
    buildWavHeader(hdr, (uint32_t)wavBufLen);
    const size_t fileSize = sizeof(hdr) + wavBufLen;
    server.sendHeader("Content-Disposition", "attachment; filename=bob_mic.wav");
    server.setContentLength(fileSize);
    server.send(200, "audio/wav", "");
    WiFiClient client = server.client();
    client.write(hdr, sizeof(hdr));
    const uint8_t* audio = (const uint8_t*)wavBuf;
    size_t sent = 0;
    while (sent < wavBufLen && client.connected()) {
      size_t n = min((size_t)1460, wavBufLen - sent);
      size_t wrote = client.write(audio + sent, n);
      if (!wrote) break;
      sent += wrote;
      yield();
    }
    client.flush();
    dlogPrintf("record: streamed RAM WAV %u bytes", (unsigned)fileSize);
    return;
  }

  // Full profile records straight to flash.
  static const String TMP = "/_rec_tmp.wav";
  String err;
  if (!recordToFile(TMP, secs, err)) {
    dlogPrintf("record: failed — %s", err.c_str());
    server.send(500, "text/plain", "record failed: " + err);
    return;
  }

  // Stream the file back to the browser
  File f = LittleFS.open(TMP, "r");
  if (!f) { server.send(500,"text/plain","file open failed"); return; }
  size_t fileSize = f.size();
  dlogPrintf("record: streaming %u bytes", (unsigned)fileSize);

  WiFiClient client = server.client();
  server.setContentLength(fileSize);
  server.send(200, "audio/wav", "");

  uint8_t chunk[1460];
  while (f.available() && client.connected()) {
    int n = f.read(chunk, sizeof(chunk));
    if (n <= 0) break;
    client.write(chunk, n);
    yield();
  }
  f.close();
  client.flush();
}

// POST /speak  body: text=Hello&mode=voice&voice=0&vol=100
//   mode=voice -> real speech via SAM TTS (if compiled in)
//   mode=beep  -> old per-letter tone bursts
void handleSpeak() {
  String text;
  if (server.hasArg("text")) {
    text = server.arg("text");
  } else if (server.hasArg("plain")) {
    text = server.arg("plain");
  }
  text.trim();
  if (text.length() == 0) { server.send(400,"text/plain","missing text"); return; }

  // SAM TTS removed — /speak always uses the buzzer beep fallback.
  // Real speech is handled by BOB_ROBO_CONVO Mac server via POST /mactts.
  if (server.hasArg("vol")) spkVol = constrain(server.arg("vol").toInt(), 5, 100);
  server.send(200,"text/plain","ok");
  dlogPrintf("speak: beep vol=%d len=%d \"%.40s\" (heap=%u)",
             spkVol, (int)text.length(), text.c_str(), ESP.getFreeHeap());
  speakText(text);
  dlogPrintf("speak: done (heap=%u)", ESP.getFreeHeap());
}

// POST :8080/mactts — multipart WAV upload buffered in PSRAM.
// build_opt.h pins AsyncTCP to Core 0. Playback and the HTTP response run
// there, leaving loopTask free. Audible quality still needs a listening test.
// HTTP 200 follows playback so the serial Python client provides back-pressure.
#define MTT_MAX_BYTES (512u * 1024u)
static AsyncWebServerRequest* mttOwner = nullptr;
static uint8_t* mttBuf = nullptr;
static size_t mttBufLen = 0;
static int mttError = 0;
static bool mttComplete = false;

// These fields are accessed only by the single AsyncTCP service task. Never
// wait for a mutex in an upload callback: that would block the owner's upload.
static void mttRelease() {
  free(mttBuf);
  mttBuf = nullptr;
  mttOwner = nullptr;
  mttBufLen = 0;
  mttError = 0;
  mttComplete = false;
}

static void mttAsyncSetup() {
  ttsServer.on("/mactts", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      if (mttOwner != req) {
        req->send(mttOwner ? 409 : 400, "text/plain", mttOwner ? "TTS busy" : "no WAV file");
        return;
      }
      int status = mttError;
      if (!status && (!hwInitDone || (BOB_VISUAL_BRINGUP && !BOB_SPEAKER_TEST))) status = 503;
      if (!status && motion.armed) status = 409;
      if (!status && (!mttComplete || !mttBuf || mttBufLen < 44)) status = 400;
      // Accept the canonical mono PCM16 WAV written by Python wave. Reject
      // unsupported headers instead of playing header bytes or invalid rates.
      uint32_t rate = 0, pcmBytes = 0;
      if (!status) {
        memcpy(&rate, mttBuf + 24, 4);
        memcpy(&pcmBytes, mttBuf + 40, 4);
        if (memcmp(mttBuf, "RIFF", 4) || memcmp(mttBuf + 8, "WAVEfmt ", 8) ||
            mttBuf[16] != 16 || mttBuf[17] || mttBuf[18] || mttBuf[19] ||
            mttBuf[20] != 1 || mttBuf[21] || mttBuf[22] != 1 || mttBuf[23] ||
            mttBuf[34] != 16 || mttBuf[35] || memcmp(mttBuf + 36, "data", 4) ||
            rate < 4000 || rate > 48000 || !pcmBytes || (pcmBytes & 1) ||
            pcmBytes != mttBufLen - 44) status = 400;
      }
      if (status) {
        mttRelease();
        req->send(status, "text/plain", "TTS upload rejected");
        return;
      }
      dlogPrintf("mactts: buffered %u bytes rate=%u core=%d", (unsigned)mttBufLen,
                 (unsigned)rate, xPortGetCoreID());
      const int16_t* mono = (const int16_t*)(mttBuf + 44);
      size_t total = pcmBytes / 2, idx = 0;
      bool ok = i2s_set_sample_rates(I2S_SPK_NUM, rate) == ESP_OK;
      uint32_t maxWriteUs = 0;
      int16_t st[256];
      while (ok && idx < total) {
        int n = 0;
        while (n < 128 && idx < total) {
          int16_t sample = mono[idx++];
          st[n * 2] = sample;
          st[n * 2 + 1] = sample;
          n++;
        }
        size_t wr = 0;
        uint32_t started = micros();
        esp_err_t err = i2s_write(I2S_SPK_NUM, st, n * 4, &wr, pdMS_TO_TICKS(500));
        uint32_t elapsed = micros() - started;
        if (elapsed > maxWriteUs) maxWriteUs = elapsed;
        ok = err == ESP_OK && wr == (size_t)n * 4;
#if CONFIG_ASYNC_TCP_USE_WDT
        esp_task_wdt_reset();
#endif
      }
      // Let the last 8 x 64 DMA frames play before clearing/restoring rate.
      if (ok) vTaskDelay(pdMS_TO_TICKS((512u * 1000u + rate - 1) / rate + 2));
      i2s_zero_dma_buffer(I2S_SPK_NUM);
      i2s_set_sample_rates(I2S_SPK_NUM, SAMPLE_RATE);
      mttRelease();
      dlogPrintf("mactts: played ok=%d maxWriteUs=%u heap=%u psram=%u", ok,
                 (unsigned)maxWriteUs, ESP.getFreeHeap(), ESP.getFreePsram());
      req->send(ok ? 200 : 500, "text/plain", ok ? "ok" : "I2S write failed");
    },
    // Multipart file content is delivered here, WITHOUT MIME boundaries.
    [](AsyncWebServerRequest* req, String filename, size_t index,
       uint8_t* data, size_t len, bool final) {
      if (!mttOwner && index == 0) {
        mttOwner = req;
        mttBufLen = 0;
        mttComplete = false;
        mttError = req->contentLength() > MTT_MAX_BYTES + 4096 ? 413 : 0;
        if (!mttError) {
          mttBuf = (uint8_t*)ps_malloc(MTT_MAX_BYTES);
          if (!mttBuf) mttError = 503;
        }
        req->onDisconnect([req]() { if (mttOwner == req) mttRelease(); });
      }
      if (mttOwner != req || mttError) return;
      if (mttComplete || index != mttBufLen) { mttError = 400; return; }
      if (len > MTT_MAX_BYTES - mttBufLen) { mttError = 413; return; }
      memcpy(mttBuf + mttBufLen, data, len);
      mttBufLen += len;
      mttComplete = final;
    }
  );
  ttsServer.begin();
  dlogPrintf("mactts: async TTS server on port %d core=%d", TTS_PORT, CONFIG_ASYNC_TCP_RUNNING_CORE);
}

// =============================================================
//  Sound clips on flash (LittleFS)
//  Storage for BOTH long recordings and audio uploaded from the
//  browser. Everything is kept as 16-bit mono WAV, so a clip can be
//  downloaded and played anywhere, and playback needs no decoder.
// =============================================================
bool fsReady = false;

// GET /play — replay the last recording through Otto's own speaker ("parrot mode").
// Reads from /_rec_tmp.wav in flash — the same file written by handleRecord().
void handlePlay() {
  static const String TMP = "/_rec_tmp.wav";
  if (!fsReady || !LittleFS.exists(TMP)) { server.send(404,"text/plain","no recording yet"); return; }
  if (isRecording)                       { server.send(503,"text/plain","busy");             return; }
  server.send(200, "text/plain", "ok");
  dlogPrintf("play: flash file vol=%d (heap=%u)", spkVol, ESP.getFreeHeap());
  playWavFile(TMP);
}

// Only allow simple filenames — no paths, no surprises.
static bool safeName(const String& n) {
  if (n.length() == 0 || n.length() > 40) return false;
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    if (!(isalnum((int)c) || c == '_' || c == '-' || c == '.')) return false;
  }
  return true;
}

// Seconds of 16 kHz mono audio that still fit in free flash (with margin).
int maxRecordSecs() {
  if (!fsReady) return RECORD_SECS;
  size_t freeB = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (freeB < 40000) return 0;
  int s = (int)((freeB - 32768) / (SAMPLE_RATE * 2));
  return constrain(s, 0, 120);
}

// Record straight from the mic to a WAV file — length is limited by
// FLASH, not RAM, which is how we get past the ~3 s buffer limit.
bool recordToFile(const String& path, int secs, String& err) {
  if (!fsReady)   { err = "filesystem not mounted"; return false; }
  if (isRecording){ err = "busy";                   return false; }
  isRecording = true;

  File f = LittleFS.open(path, "w");
  if (!f) { isRecording = false; err = "cannot create file"; return false; }

  uint8_t hdr[44];
  buildWavHeader(hdr, 0);          // placeholder — real sizes written at the end
  f.write(hdr, 44);

  static int32_t raw[256];
  int16_t out[256];
  const size_t need = (size_t)SAMPLE_RATE * (size_t)secs;
  size_t  total = 0;
  int32_t dcIn = 0, dcOut = 0;     // one-pole DC blocker (streaming-friendly)

  i2s_zero_dma_buffer(I2S_MIC_NUM);
  size_t junk = SAMPLE_RATE / 20;  // drop ~50 ms of start-up garbage
  while (junk > 0) {
    size_t br = 0;
    if (i2s_read(I2S_MIC_NUM, raw, sizeof(raw), &br, pdMS_TO_TICKS(200)) != ESP_OK) break;
    size_t got = br / sizeof(int32_t);
    if (!got) break;
    junk = (got >= junk) ? 0 : junk - got;
  }

  unsigned long t0 = millis();
  while (total < need) {
    size_t br = 0;
    if (i2s_read(I2S_MIC_NUM, raw, sizeof(raw), &br, pdMS_TO_TICKS(200)) != ESP_OK) break;
    size_t got = br / sizeof(int32_t);
    if (!got) break;
    size_t n = 0;
    for (size_t i = 0; i < got && total < need; i++, total++, n++) {
      int32_t s = raw[i] >> micShift;
      int32_t y = s - dcIn + (dcOut * 127) / 128;   // removes the mic's DC offset
      dcIn = s; dcOut = y;
      out[n] = (int16_t)constrain(y, -32768, 32767);
    }
    if (n && f.write((uint8_t*)out, n * 2) != n * 2) { err = "flash full"; break; }
    yield();
    if (millis() - t0 > (unsigned long)(secs + 3) * 1000UL) break;
  }

  uint32_t dataBytes = (uint32_t)total * 2;
  buildWavHeader(hdr, dataBytes);
  f.seek(0); f.write(hdr, 44);
  f.close();
  isRecording = false;
  dlogPrintf("recfile: %s %u bytes (%d s asked)", path.c_str(), (unsigned)dataBytes, secs);
  if (dataBytes == 0) { if (!err.length()) err = "mic returned no data"; return false; }
  return true;
}

// Play a WAV file from flash out of the MAX98357A. Handles mono or stereo
// and any sample rate in the header; restores the speaker rate afterwards.
void playWavFile(const String& path) {
  File f = LittleFS.open(path, "r");
  if (!f) { dlogPrintf("playfile: %s not found", path.c_str()); return; }
  uint8_t hdr[44];
  if (f.read(hdr, 44) != 44) { f.close(); return; }
  uint32_t rate  = (uint32_t)hdr[24] | ((uint32_t)hdr[25]<<8) | ((uint32_t)hdr[26]<<16) | ((uint32_t)hdr[27]<<24);
  uint16_t chans = (uint16_t)hdr[22] | ((uint16_t)hdr[23]<<8);
  if (rate < 4000 || rate > 48000) rate = SAMPLE_RATE;
  if (chans != 1 && chans != 2)    chans = 1;
  dlogPrintf("playfile: %s rate=%u ch=%u vol=%d", path.c_str(), (unsigned)rate, (unsigned)chans, spkVol);

  i2s_set_sample_rates(I2S_SPK_NUM, rate);
  int16_t in[64], chunk[128];
  while (true) {
    int n = f.read((uint8_t*)in, sizeof(in));
    if (n <= 0) break;
    int samples = n / 2;
    size_t wr = 0;
    if (chans == 2) {                       // already stereo — write as-is
      for (int k = 0; k < samples; k++) in[k] = (int16_t)((int32_t)in[k] * spkVol / 100);
      i2s_write(I2S_SPK_NUM, in, samples * 2, &wr, pdMS_TO_TICKS(500));
    } else {                                // mono — duplicate into both slots
      for (int k = 0; k < samples; k++) {
        int32_t v = (int32_t)in[k] * spkVol / 100;
        chunk[k*2] = (int16_t)v; chunk[k*2+1] = (int16_t)v;
      }
      i2s_write(I2S_SPK_NUM, chunk, samples * 4, &wr, pdMS_TO_TICKS(500));
    }
    yield();
  }
  f.close();
  i2s_zero_dma_buffer(I2S_SPK_NUM);
  i2s_set_sample_rates(I2S_SPK_NUM, SAMPLE_RATE);
}

// GET /files — clip list + storage usage
void handleFiles() {
  String j = "{\"ok\":"; j += fsReady ? 1 : 0;
  j += ",\"total\":"; j += (unsigned long)(fsReady ? LittleFS.totalBytes() : 0);
  j += ",\"used\":";  j += (unsigned long)(fsReady ? LittleFS.usedBytes()  : 0);
  j += ",\"maxsecs\":"; j += maxRecordSecs();
  j += ",\"files\":[";
  if (fsReady) {
    File root = LittleFS.open("/");
    File e = root.openNextFile();
    bool first = true;
    while (e) {
      if (!e.isDirectory()) {
        String nm = e.name();
        if (nm.startsWith("/")) nm = nm.substring(1);
        if (!first) j += ",";
        first = false;
        j += "{\"n\":\""; j += nm; j += "\",\"s\":"; j += (unsigned long)e.size(); j += "}";
      }
      e = root.openNextFile();
    }
  }
  j += "]}";
  server.send(200, "application/json", j);
}

// POST /upload — multipart file upload straight to flash (streamed, so a
// clip much bigger than free RAM is fine).
static File   uploadFile;
static String uploadErr;

void handleUploadData() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadErr = "";
    if (!fsReady) { uploadErr = "filesystem not mounted"; return; }
    String n = up.filename;
    int slash = n.lastIndexOf('/'); if (slash >= 0) n = n.substring(slash + 1);
    slash = n.lastIndexOf('\\');    if (slash >= 0) n = n.substring(slash + 1);
    if (!safeName(n)) { uploadErr = "bad filename"; return; }
    if (LittleFS.exists("/" + n)) LittleFS.remove("/" + n);
    uploadFile = LittleFS.open("/" + n, "w");
    if (!uploadFile) uploadErr = "cannot create file";
    dlogPrintf("upload: start %s", n.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && uploadErr.length() == 0) {
      if (uploadFile.write(up.buf, up.currentSize) != up.currentSize) uploadErr = "flash full";
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    dlogPrintf("upload: end %u bytes err=%s", (unsigned)up.totalSize,
               uploadErr.length() ? uploadErr.c_str() : "none");
  }
}

// POST /streamplay — play audio with NO storage at all.
// The browser decodes the MP3 and POSTs raw WAV; each chunk goes straight
// from the network into the I2S DMA as it arrives. Nothing touches flash,
// so length is unlimited (a whole song works) — it only needs the phone to
// stay connected for the duration.
static uint8_t  strmHdr[44];
static int      strmHdrGot   = 0;
static bool     strmRateSet  = false;
static bool     strmCarryHas = false;
static uint8_t  strmCarry    = 0;
static uint32_t strmBytes    = 0;

void handleStreamData() {
  HTTPUpload& up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    strmHdrGot = 0; strmRateSet = false; strmCarryHas = false; strmBytes = 0;
    dlogPrintf("streamplay: start (heap=%u)", ESP.getFreeHeap());
    return;
  }

  if (up.status == UPLOAD_FILE_WRITE) {
    size_t i = 0;

    // Swallow the 44-byte WAV header, then set the speaker to its sample rate
    while (strmHdrGot < 44 && i < up.currentSize) strmHdr[strmHdrGot++] = up.buf[i++];
    if (strmHdrGot == 44 && !strmRateSet) {
      uint32_t rate = (uint32_t)strmHdr[24] | ((uint32_t)strmHdr[25]<<8)
                    | ((uint32_t)strmHdr[26]<<16) | ((uint32_t)strmHdr[27]<<24);
      if (rate < 4000 || rate > 48000) rate = SAMPLE_RATE;
      i2s_set_sample_rates(I2S_SPK_NUM, rate);
      strmRateSet = true;
      dlogPrintf("streamplay: rate=%u vol=%d", (unsigned)rate, spkVol);
    }

    // Everything after the header is 16-bit mono PCM. Chunk boundaries can
    // split a sample in half, so carry the odd byte over to the next chunk.
    while (i < up.currentSize) {
      int16_t mono[64];
      int     n = 0;
      while (n < 64 && i < up.currentSize) {
        if (strmCarryHas) {
          mono[n++] = (int16_t)((uint16_t)strmCarry | ((uint16_t)up.buf[i++] << 8));
          strmCarryHas = false;
        } else if (i + 1 < up.currentSize) {
          mono[n++] = (int16_t)((uint16_t)up.buf[i] | ((uint16_t)up.buf[i+1] << 8));
          i += 2;
        } else {
          strmCarry = up.buf[i++]; strmCarryHas = true;
        }
      }
      if (n) {
        int16_t st[128];                       // mono -> stereo pairs
        for (int k = 0; k < n; k++) {
          int32_t v = (int32_t)mono[k] * spkVol / 100;
          st[k*2] = (int16_t)v; st[k*2+1] = (int16_t)v;
        }
        size_t wr = 0;
        i2s_write(I2S_SPK_NUM, st, n * 4, &wr, pdMS_TO_TICKS(1000));
        strmBytes += (uint32_t)n * 2;
      }
    }
    return;
  }

  if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    i2s_zero_dma_buffer(I2S_SPK_NUM);
    i2s_set_sample_rates(I2S_SPK_NUM, SAMPLE_RATE);
    dlogPrintf("streamplay: done %u bytes (%.1f s)",
               (unsigned)strmBytes, strmBytes / 32000.0f);
  }
}

// GET /playfile?name=x.wav — play a stored clip on the robot's speaker
void handlePlayFile() {
  String n = server.arg("name");
  if (!fsReady || !safeName(n) || !LittleFS.exists("/" + n)) {
    server.send(404, "text/plain", "no such clip"); return;
  }
  server.send(200, "text/plain", "ok");
  playWavFile("/" + n);
}

// GET /clip?name=x.wav — download / stream a clip to the browser
void handleClipGet() {
  String n = server.arg("name");
  if (!fsReady || !safeName(n) || !LittleFS.exists("/" + n)) {
    server.send(404, "text/plain", "no such clip"); return;
  }
  File f = LittleFS.open("/" + n, "r");
  server.streamFile(f, "audio/wav");
  f.close();
}

// GET /delfile?name=x.wav
void handleDelFile() {
  String n = server.arg("name");
  if (!fsReady || !safeName(n)) { server.send(400, "text/plain", "bad name"); return; }
  bool ok = LittleFS.remove("/" + n);
  dlogPrintf("delfile: %s %s", n.c_str(), ok ? "deleted" : "FAILED");
  server.send(ok ? 200 : 404, "text/plain", ok ? "ok" : "not found");
}

// GET /recfile?secs=N&name=x.wav — long recording straight to flash
void handleRecFile() {
  int  secs = server.hasArg("secs") ? server.arg("secs").toInt() : 5;
  int  cap  = maxRecordSecs();
  if (cap <= 0) { server.send(507, "text/plain", "flash full — delete a clip first"); return; }
  secs = constrain(secs, 1, cap);
  String n = server.hasArg("name") ? server.arg("name") : String("rec.wav");
  if (!n.endsWith(".wav")) n += ".wav";
  if (!safeName(n)) { server.send(400, "text/plain", "bad name"); return; }

  // Long capture to flash — same deal as /record: paint the ear before the
  // blocking call so there is feedback for the whole recording.
  listenUntil = millis() + (unsigned long)(secs + 3) * 1000UL;
  prevListen  = true;
  drawFaceNow();

  String err;
  if (!recordToFile("/" + n, secs, err)) {
    server.send(500, "text/plain", err.length() ? err : "record failed");
    return;
  }
  server.send(200, "text/plain", n);
}

// GET /micconfig?ch=0|1&fmt=0|1&fix=0|1&shift=11|14|16
// Re-installs the mic driver live with the given settings, saves to NVS.
void handleMicConfig() {
  if (server.hasArg("ch"))    micCh    = server.arg("ch").toInt()  ? 1 : 0;
  if (server.hasArg("fmt"))   micFmt   = server.arg("fmt").toInt() ? 1 : 0;
  if (server.hasArg("fix"))   micFix   = server.arg("fix").toInt() ? 1 : 0;
  if (server.hasArg("shift")) micShift = constrain(server.arg("shift").toInt(), 8, 16);
  i2s_mic_init();
  saveMicCfg();
  String j = "{\"ch\":";  j += micCh;
  j += ",\"fmt\":";       j += micFmt;
  j += ",\"fix\":";       j += micFix;
  j += ",\"shift\":";     j += micShift;
  j += "}";
  server.send(200, "application/json", j);
}

// GET /micscan — try ALL 8 mic I2S configs (~3 s), report signal for each,
// then apply + save the best one. Rules out every software combination.
void handleMicScan() {
  dlogPrintf("micscan: start (heap=%u)", ESP.getFreeHeap());
  String r = "=== MIC CONFIG AUTO-SCAN ===\r\n";
  r += "(make noise near the mic while this runs)\r\n\r\n";
  static int32_t raw[256];
  long    bestScore = 0;
  uint8_t bestCh = micCh, bestFmt = micFmt, bestFix = micFix;

  for (uint8_t ch = 0; ch < 2; ch++)
    for (uint8_t fmt = 0; fmt < 2; fmt++)
      for (uint8_t fix = 0; fix < 2; fix++) {
        micCh = ch; micFmt = fmt; micFix = fix;
        i2s_mic_init();
        delay(30);
        size_t br = 0;
        i2s_read(I2S_MIC_NUM, raw, sizeof(raw), &br, pdMS_TO_TICKS(200));  // discard settle junk
        int32_t mn = 0, mx = 0;
        bool    first = true;
        size_t  nz = 0, total = 0;
        unsigned long t0 = millis();
        while (millis() - t0 < 150) {
          if (i2s_read(I2S_MIC_NUM, raw, sizeof(raw), &br, pdMS_TO_TICKS(100)) != ESP_OK) break;
          size_t got = br / sizeof(int32_t);
          if (!got) break;
          for (size_t i = 0; i < got; i++) {
            int32_t s = raw[i];
            if (first) { mn = mx = s; first = false; }
            if (s < mn) mn = s;
            if (s > mx) mx = s;
            if (s != 0) nz++;
            total++;
          }
          yield();
        }
        long spread = (long)((mx >> 14) - (mn >> 14));
        char line[100];
        snprintf(line, sizeof(line), "ch=%-5s fmt=%-3s fix=%d  nonzero=%4u/%4u  spread=%-6ld %s\r\n",
                 ch ? "RIGHT" : "LEFT", fmt ? "MSB" : "I2S", fix,
                 (unsigned)nz, (unsigned)total, spread,
                 (nz > 0 && spread > 0) ? "<<< SIGNAL" : "dead");
        r += line;
        long score = (nz > 0) ? spread : 0;
        if (score > bestScore) { bestScore = score; bestCh = ch; bestFmt = fmt; bestFix = fix; }
      }

  micCh = bestCh; micFmt = bestFmt; micFix = bestFix;
  i2s_mic_init();
  r += "\r\n";
  if (bestScore > 0) {
    saveMicCfg();
    r += "BEST CONFIG: ch="; r += bestCh ? "RIGHT" : "LEFT";
    r += " fmt=";            r += bestFmt ? "MSB" : "I2S";
    r += " fix=";            r += bestFix;
    r += "  -> APPLIED & SAVED\r\n";
    r += "Now turn on the Mic Meter and talk to confirm.\r\n";
  } else {
    r += "RESULT: ALL 8 configs read pure zero.\r\n";
    r += "No software setting can cause that -> it is 100% hardware.\r\n";
    r += "Check IN THIS ORDER (multimeter or swap wires):\r\n";
    r += " 1. Mic 3V pin: measure 3.3V against GND at the MIC end of the wire\r\n";
    r += " 2. DOUT jumper: continuity from mic DOUT pad to ESP32 GPIO48 pin\r\n";
    r += "    (jumper wires break inside the plastic — try a NEW wire)\r\n";
    r += " 3. SEL pad tied to GND (not floating)\r\n";
    r += " 4. Header pins actually soldered? Loose press-fit pins on the\r\n";
    r += "    breakout are the #1 cause of exactly this symptom.\r\n";
    r += " 5. If all of the above check out, the mic module is dead.\r\n";
  }
  server.send(200, "text/plain", r);
}

// =============================================================
//  WiFi credentials — NVS load / save
// =============================================================
void loadWifiCreds() {
  prefs.begin("otto", true);
  String s = prefs.getString("wssid", "");
  String p = prefs.getString("wpass", "");
  prefs.end();
  // Saved-from-UI credentials win; otherwise keep the compiled-in fallback.
  if (s.length()) { wifiHomeSsid = s; wifiHomePass = p; }
  dlogPrintf("wifi: home creds ssid=\"%s\" (%s)",
             wifiHomeSsid.c_str(),
             s.length() ? "saved from UI" : "compiled-in default");
}

void saveWifiCreds(const String& ssid, const String& pass) {
  prefs.begin("otto", false);
  prefs.putString("wssid", ssid);
  prefs.putString("wpass", pass);
  prefs.end();
}

// =============================================================
//  WiFi — always start own AP, then optionally join home network
// =============================================================
// The AP (Anvyxo_robo / bob12345 @ 192.168.4.1) is ALWAYS started so the UI
// is reachable from the robot's own hotspot even if home WiFi is wrong or
// unavailable. When home credentials are saved, we also try STA mode;
// success means we run AP+STA simultaneously so neither connection drops.
void wifiConnect() {
  WiFi.persistent(false);      // never let the SDK write its own NVS credentials
  WiFi.setSleep(false);        // keeps the web UI responsive

  // ── Step 1: bring up the built-in AP unconditionally ─────────
  WiFi.mode(WIFI_AP_STA);      // AP+STA so both can coexist
  bool apOk = WiFi.softAP(AP_SSID, AP_PASS);
  ottoIP = WiFi.softAPIP().toString();   // always 192.168.4.1
  dlogPrintf("wifi: AP %s  ssid=\"%s\"  ip=%s", apOk ? "up" : "FAILED",
             AP_SSID, ottoIP.c_str());
  if (apOk) hwSet(HW_WIFI_AP, HW_OK,   "%s @ %s", AP_SSID, ottoIP.c_str());
  else      hwSet(HW_WIFI_AP, HW_FAIL, "softAP refused (password must be 8+ chars)");

  // ── Step 2: try home WiFi if credentials have been saved ─────
  wifiHomeOk = false;
  homeIP     = "";
  if (wifiHomeSsid.length() > 0) {
    WiFi.setHostname("anvyxo-robo");
    WiFi.setAutoReconnect(true);
    WiFi.begin(wifiHomeSsid.c_str(), wifiHomePass.c_str());
    dlogPrintf("wifi: joining home \"%s\" ...", wifiHomeSsid.c_str());

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_JOIN_TIMEOUT_MS) {
      delay(250);
      yield();
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiHomeOk = true;
      homeIP     = WiFi.localIP().toString();
      // THROUGHPUT FIX: running the AP and STA together on the single radio
      // throttled the home link to ~1 KB/s — TTS uploads took 10 s+ and the TFT
      // froze while they ran. Once we're on home WiFi the hotspot isn't needed,
      // so drop it and run STA-only for full bandwidth. wifiStatusTick() brings
      // the AP back if the home link later drops, so we're never unreachable.
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      WiFi.setTxPower(WIFI_POWER_19_5dBm);     // max TX power for a stronger link
      apMode = false;
      hwSet(HW_WIFI_AP, HW_NOCHECK, "off (STA-only for speed)");
      dlogPrintf("wifi: home joined  ip=%s  rssi=%d dBm — AP dropped (STA-only)",
                 homeIP.c_str(), WiFi.RSSI());
    } else {
      dlogPrintf("wifi: home join FAILED (status=%d) — AP still up",
                 (int)WiFi.status());
      WiFi.disconnect(false);   // drop STA attempt, keep AP
    }
  }
  if (wifiHomeOk)                hwSet(HW_WIFI_HOME, HW_OK,   "%s @ %s", wifiHomeSsid.c_str(), homeIP.c_str());
  else if (wifiHomeSsid.length())hwSet(HW_WIFI_HOME, HW_FAIL, "could not join \"%s\"", wifiHomeSsid.c_str());
  else                           hwSet(HW_WIFI_HOME, HW_FAIL, "no home WiFi saved");

  if (MDNS.begin("anvyxo-robo")) {
    MDNS.addService("http", "tcp", WEB_PORT);
    dlogPrintf("wifi: mDNS up — http://anvyxo-robo.local");
  }
}

// Show the current reachable address for five seconds without blocking
// camera, servo commands or the web server. The face returns automatically.
void showNetScreen() {
  const bool joined = wifiHomeOk && homeIP.length();
  const String ip = joined ? homeIP : ottoIP;
  tft.fillScreen(C_BLACK);
  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setTextColor(C_GREEN, C_BLACK);
  tft.setCursor(4, 6);
  tft.print("ANVYXO / NETWORK STATUS");
  tft.setCursor(4, 24);
  tft.print(joined ? "[ OK ] WiFi connected" : "[ AP ] Hotspot mode");
  tft.setCursor(4, 38);
  tft.print(joined ? wifiHomeSsid.substring(0, 25) : String(AP_SSID));
  tft.setCursor(4, 54);
  tft.print("Open in browser:");
  tft.setTextSize(ip.length() <= 12 ? 2 : 1); // longest IPv4 must fit 160px
  tft.setCursor(4, 68);
  tft.print(ip.length() ? ip : String("IP unavailable"));
  tft.setTextSize(1);
  tft.setCursor(4, 94);
  tft.print(joined ? "AP also: 192.168.4.1" : "Home WiFi not connected");
  tft.setCursor(4, 110);
  tft.print(joined ? "http://anvyxo-robo.local" : "Join Anvyxo_robo to control");
  // One-tap-recalibrate policy: the saved IMU baseline is never trusted at boot
  // (a robot powered on lying down would read as "lifted"). Prompt for a fresh
  // calibration every start; motors still hold the saved calibrated pose.
#if BOB_IMU_TEST
  if (!imuCalValid) {
    tft.setTextColor(C_STAR, C_BLACK);   // yellow
    tft.setCursor(4, 120);
    tft.print("! Calibrate IMU (web)");
    tft.setTextColor(C_GREEN, C_BLACK);
  }
#endif
  netScreenUntil = millis() + 5000;
}

// AP is always enabled, so monitoring must not be gated by !apMode.
// Run on the main loop (never draw the TFT inside a WiFi event callback).
void wifiStatusTick() {
  static unsigned long lastCheck = 0;
  static unsigned long lastRetry = 0;
  const unsigned long now = millis();
  if (now - lastCheck < 1000) return;
  lastCheck = now;
  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
  bool joined = ip.length() && ip != "0.0.0.0";
  if (!joined) ip = "";
  if (joined != wifiHomeOk || ip != homeIP) {
    wifiHomeOk = joined;
    homeIP = ip;
    // Keep the radio in the fastest safe mode (see wifiConnect's throughput fix):
    //   • home up  → STA-only (drop the AP) for full bandwidth
    //   • home down → re-raise the AP so the robot stays reachable
    if (joined && apMode) {              // joined and AP still up → drop AP for speed
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      WiFi.setTxPower(WIFI_POWER_19_5dBm);
      apMode = false;
      hwSet(HW_WIFI_AP, HW_NOCHECK, "off (STA-only for speed)");
      dlogPrintf("wifi: home back — AP dropped (STA-only)");
    } else if (!joined && !apMode) {     // home lost and we were STA-only → fallback AP
      WiFi.mode(WIFI_AP_STA);
      WiFi.softAP(AP_SSID, AP_PASS);
      ottoIP = WiFi.softAPIP().toString();
      apMode = true;
      hwSet(HW_WIFI_AP, HW_OK, "%s @ %s", AP_SSID, ottoIP.c_str());
      dlogPrintf("wifi: home lost — AP re-raised as fallback @ %s", ottoIP.c_str());
    }
    hwSet(HW_WIFI_HOME, joined ? HW_OK : HW_FAIL,
          joined ? "connected: %s" : "offline; use hotspot %s",
          joined ? homeIP.c_str() : ottoIP.c_str());
    dlogPrintf("wifi: display updated - %s IP=%s", joined ? "home" : "hotspot",
               joined ? homeIP.c_str() : ottoIP.c_str());
    showNetScreen();
  }
  if (!joined && wifiHomeSsid.length() && now - lastRetry >= 30000) {
    lastRetry = now;
    WiFi.reconnect();
  }
}

// =============================================================
//  POST /wifisave — save new home WiFi credentials and reconnect.
//  Body: ssid=<name>&pass=<password>  (URL-encoded form data)
//  The robot saves to NVS, replies immediately so the browser gets
//  a response before the STA reconnect blocks for up to 15 s, then
//  reconnects and shows the new IP on the TFT.
// =============================================================
void handleWifiSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim(); pass.trim();

  if (ssid.length() == 0) {
    server.send(400, "text/plain", "ssid required");
    return;
  }
  if (ssid.length() > 32 || pass.length() > 63) {
    server.send(400, "text/plain", "ssid max 32 chars, pass max 63 chars");
    return;
  }

  saveWifiCreds(ssid, pass);
  wifiHomeSsid = ssid;
  wifiHomePass = pass;
  dlogPrintf("wifisave: ssid=\"%s\" saved — reconnecting", ssid.c_str());

  // Reply BEFORE blocking on the join attempt so the browser gets a response.
  server.send(200, "text/plain", "saved — connecting to \"" + ssid + "\"");

  // Drop any existing STA connection and try the new one.
  WiFi.disconnect(false);
  delay(200);
  WiFi.setHostname("anvyxo-robo");
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_JOIN_TIMEOUT_MS) {
    delay(250);
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiHomeOk = true;
    homeIP     = WiFi.localIP().toString();
    dlogPrintf("wifisave: connected  ip=%s", homeIP.c_str());
    hwSet(HW_WIFI_HOME, HW_OK, "%s @ %s", ssid.c_str(), homeIP.c_str());
    // Show the new IP on the TFT for 5 s so the user can see it.
    showNetScreen();
  } else {
    wifiHomeOk = false;
    homeIP     = "";
    dlogPrintf("wifisave: join FAILED (status=%d)", (int)WiFi.status());
    hwSet(HW_WIFI_HOME, HW_FAIL, "could not join \"%s\"", ssid.c_str());
    WiFi.disconnect(false);
    showNetScreen(); // keep showing the reachable hotspot address
  }
}

// GET /wifistatus — returns current WiFi state as JSON.
// Polled by the UI after a save to show the result.
void handleWifiStatus() {
  String j = "{\"ap\":\""; j += AP_SSID;
  j += "\",\"apIp\":\"";   j += ottoIP;
  j += "\",\"homeOk\":";   j += wifiHomeOk ? 1 : 0;
  j += ",\"homeSsid\":\""; j += wifiHomeSsid;
  j += "\",\"homeIp\":\""; j += homeIP;
  j += "\"}";
  server.send(200, "application/json", j);
}

// =============================================================
//  MPU-6500 / MPU-9250 accelerometer and gyroscope
// =============================================================
//
//  ±2 g = 16384 raw LSB/g; ±250 °/s = 131 raw LSB/(°/s).
//  Convert to the existing UI/lift units: 1 g = 981, 1 °/s = 16.
//  Pitch and roll remain accelerometer-based; yaw/magnetometer is not used.
static uint8_t imuAddr = 0;
static uint8_t imuWho = 0;
static const char* imuChipName() {
  return imuWho == 0x70 ? "MPU-6500" : (imuWho == 0x73 ? "MPU-9255" : "MPU-9250");
}

// Raw sensor values (updated every IMU_PERIOD_MS in loop).
// Stored as scaled int16 so the rest of the sketch (lift detection, calibration,
// /imu JSON, UI bars) needs zero changes.
//   accel: float m/s² × 100  → int16  (1 g ≈ 981)
//   gyro : float °/s  × 16   → int16  (1 °/s = 16)
int16_t imu_ax = 0, imu_ay = 0, imu_az = 0;   // accelerometer scaled LSB
int16_t imu_gx = 0, imu_gy = 0, imu_gz = 0;   // gyroscope    scaled LSB
bool    imuReady   = false;   // true while the chip is answering on the bus
bool    imuLifted  = false;   // true when robot is detected off the ground

// ── Calibration baseline ──────────────────────────────────────
// At-rest average of accel axes sampled during /imucal.
// Stored in NVS so it survives power-off.
// Lift detection is done against the calibrated rest magnitude, so
// the robot can be placed at any angle and detection still works.
// Default: flat on table, 1 g pointing up → az ≈ +981 LSB, ax/ay ≈ 0.
int16_t imuCalAx = 0, imuCalAy = 0, imuCalAz = 981;
// Gyro zero-rate bias measured during /imucal.
int16_t imuCalGx = 0, imuCalGy = 0, imuCalGz = 0;
bool    imuCalValid = false;   // false until first /imucal or NVS load

// Master on/off for the pick-up / shake reaction (scared face → foot flips →
// auto-recalibrate). Toggled from the web UI via /liftdetect?on=0|1; the "on"
// path auto-calibrates so the feature is armed the instant it is switched on.
// Persisted in NVS so the choice survives a reboot. Even when true, nothing
// fires until imuCalValid is set — the loop() gate needs BOTH.
bool    liftDetectEnabled = true;

// Pitch/roll of the calibrated rest pose, in degrees. Everything the UI shows
// is reported RELATIVE to this, so "Calibrate" zeroes the whole panel and parks
// the 3D cube on its true neutral face no matter how the IMU is mounted.
float   imuCalPitch = 0.0f, imuCalRoll = 0.0f;

// imuLiftDelta: how many LSB the total acceleration magnitude must deviate
// from the rest magnitude before lift is declared.
// 1 g = 981 LSB (accel × 100 scaling).  Default 240 ≈ 0.24 g margin.
// User-adjustable via /imucal?thresh=N  (range 60–3000, UI slider maps to it).
int     imuLiftDelta = 240;

// imuShakeGyro: gyro-magnitude threshold (bias-removed) that also declares a
// "hit". Accel magnitude can't sense a shake (gravity's magnitude is constant),
// so we ALSO watch rotation. imu_g* are in °/s × 16, so 600 ≈ 37 °/s — well
// above the ~4 °/s rest noise, so any real shake/pick-up wobble triggers.
int     imuShakeGyro = 600;
// Diagnostic: peak gyro magnitude (bias-removed) seen since the last /imu read.
// Lets a slow HTTP poller capture the true shake peak the fast loop sees.
long    imuGmagPeak  = 0;

// Cached rest magnitude (|cal_vector|) — computed once after calibration.
// At rest on a flat surface ≈ 981 (1 g).  We compare current |accel| to this.
long    imuRestMag = 981;

static unsigned long nextImuRead = 0;
#define IMU_PERIOD_MS   50    // read at 20 Hz — plenty for lift detection
#define IMU_CAL_SAMPLES 60    // averages ~3 s of readings for the baseline

// ── Link health ───────────────────────────────────────────────
// The IMU is a hot-pluggable passenger, not a dependency. If its wiring shakes
// loose mid-dance the ONLY thing that may stop is lift detection — the web UI,
// the face, the servos and the log all carry on.
//
// On repeated read failure we stop polling at 20 Hz and drop back to a slow
// probe every IMU_RETRY_MS so server.handleClient() is never starved.
int      imuFailStreak  = 0;      // consecutive failed reads
uint32_t imuReconnects  = 0;      // times the chip came back after dropping out
unsigned long imuLastOkMs = 0;    // millis() of the last successful read
#define IMU_FAIL_LIMIT      5     // 5 bad reads (~250 ms) before declaring it gone
#define IMU_RETRY_MS     2000     // while offline, re-probe this often — no faster

// ── Lift reaction phase ───────────────────────────────────────
// (enum ReactState and `reactState` are declared earlier, with the scared-mode
// state, so handleState()/handleImu() can see them.)

static bool imuReadRegs(uint8_t addr, uint8_t reg, uint8_t* out, size_t count) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)count) != count) return false;
  for (size_t i = 0; i < count; ++i) out[i] = Wire.read();
  return true;
}

static bool imuWriteReg(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Called once at boot and every two seconds while offline. Missing hardware
// leaves camera, screen, servos and audio usable.
bool imuInit(bool verbose) {
  static bool wireStarted = false;
  if (!wireStarted) {
    Wire.begin(IMU_SDA, IMU_SCL);
    Wire.setClock(100000);  // conservative for jumper wires and breakout pull-ups
    Wire.setTimeOut(20);
    wireStarted = true;
  }

  uint8_t id = 0;
  uint8_t addr = 0;
  for (uint8_t candidate = 0x68; candidate <= 0x69; ++candidate) {
    if (imuReadRegs(candidate, 0x75, &id, 1)) {
      if (id == 0x70 || id == 0x71 || id == 0x73) {
        addr = candidate;
        break;
      }
      if (verbose)
        dlogPrintf("imu: unsupported WHO_AM_I=0x%02X at 0x%02X (expected 0x70/0x71/0x73)",
                   id, candidate);
    }
  }
  if (!addr) {
    if (verbose)
      dlogPrintf("imu: no MPU-6500/9250 at 0x68/0x69; check SDA=%d SCL=%d power and AD0",
                 IMU_SDA, IMU_SCL);
    return false;
  }

  // Wake all six axes, use gyro PLL clock, ±2 g and ±250 °/s.
  bool configured =
    imuWriteReg(addr, 0x6B, 0x01) &&  // PWR_MGMT_1: clock select, clear sleep
    imuWriteReg(addr, 0x6C, 0x00) &&  // PWR_MGMT_2: all axes enabled
    imuWriteReg(addr, 0x1A, 0x03) &&  // CONFIG: low-pass filter
    imuWriteReg(addr, 0x1B, 0x00) &&  // GYRO_CONFIG: ±250 °/s
    imuWriteReg(addr, 0x1C, 0x00) &&  // ACCEL_CONFIG: ±2 g
    imuWriteReg(addr, 0x1D, 0x03);    // ACCEL_CONFIG2: low-pass filter
  uint8_t power = 0;
  if (!configured || !imuReadRegs(addr, 0x6B, &power, 1) || (power & 0x40)) {
    if (verbose) dlogPrintf("imu: chip 0x%02X answered but configuration failed", addr);
    return false;
  }
  imuAddr = addr;
  imuWho = id;
  if (verbose)
    dlogPrintf("imu: %s detected (WHO_AM_I=0x%02X addr=0x%02X SDA=%d SCL=%d)",
               imuChipName(), imuWho, imuAddr, IMU_SDA, IMU_SCL);
  return true;
}

// Pitch/roll of an acceleration vector, in degrees.
//   pitch = tilt forward/back,  roll = tilt left/right
// Shared by the live reading and the calibration baseline so the two are always
// computed the same way and their difference is meaningful.
// ── One knob to flip tilt direction EVERYWHERE ───────────────────────────────
// accelPitchRoll() is the single source every tilt consumer reads: the IMU cube
// (/imu), the "tilt left/right" reactions (/state), the tilt-drive + orientation
// card (/imupose) and the IMU-driven motion moves all derive pitch/roll from
// here, and the calibration baseline (imuCalPitch/imuCalRoll) does too. So
// negating a sign here reverses the CUBE motion, the LEFT/RIGHT reactions AND
// the leaning MOTION all at once — they can never drift out of sync. Flip an
// axis to -1 if a remount or wiring change inverts that direction; +1 = as-is.
#define IMU_PITCH_SIGN  (+1)
#define IMU_ROLL_SIGN   (+1)

static void accelPitchRoll(int16_t ax, int16_t ay, int16_t az,
                           float* pitch, float* roll) {
  float fax = (float)ax, fay = (float)ay, faz = (float)az;
  *pitch = IMU_PITCH_SIGN * (atan2f(fax, sqrtf(fay*fay + faz*faz)) * 180.0f / PI);
  *roll  = IMU_ROLL_SIGN  * (atan2f(fay, faz) * 180.0f / PI);
}

// Wrap an angle difference into -180..+180 so a pose calibrated near ±180°
// does not report a 359° deviation the moment it nudges across the wrap.
static float angleDelta(float a, float b) {
  float d = a - b;
  while (d >  180.0f) d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  return d;
}

// Read the contiguous accel/temp/gyro registers in one I²C transaction.
// Convert to the sketch's existing UI/lift units.
bool imuReadRaw() {
  if (!imuAddr) return false;
  uint8_t raw[14];
  if (!imuReadRegs(imuAddr, 0x3B, raw, sizeof(raw))) return false;
  auto s16 = [](uint8_t hi, uint8_t lo) -> int16_t {
    return (int16_t)((uint16_t(hi) << 8) | lo);
  };
  int16_t nax = (int32_t)s16(raw[0],  raw[1])  * 981 / 16384;
  int16_t nay = (int32_t)s16(raw[2],  raw[3])  * 981 / 16384;
  int16_t naz = (int32_t)s16(raw[4],  raw[5])  * 981 / 16384;
  // Median-of-3 despike per accel axis. This MPU emits ~3% single-sample
  // spikes (magnitude jumping 580..2180 vs ~980 at rest), worse during servo
  // motion. A lone outlier is outvoted by its two neighbours, so pitch/roll,
  // the calibration baseline and the tip-safety see clean data. ~1 sample
  // (50 ms) of lag, which is fine for 20 Hz posture use.
  auto med3 = [](int16_t a, int16_t b, int16_t c) -> int16_t {
    return a>b ? (b>c ? b : (a>c ? c : a)) : (a>c ? a : (b>c ? c : b));
  };
  static int16_t ha[3]={0,0,0}, hb[3]={0,0,0}, hc[3]={0,0,0};
  static uint8_t hp=0; static bool hfull=false;
  ha[hp]=nax; hb[hp]=nay; hc[hp]=naz; hp=(hp+1)%3; if(hp==0) hfull=true;
  if (hfull) { imu_ax=med3(ha[0],ha[1],ha[2]); imu_ay=med3(hb[0],hb[1],hb[2]); imu_az=med3(hc[0],hc[1],hc[2]); }
  else       { imu_ax=nax; imu_ay=nay; imu_az=naz; }
  imu_gx = (int32_t)s16(raw[8],  raw[9])  * 16 / 131;
  imu_gy = (int32_t)s16(raw[10], raw[11]) * 16 / 131;
  imu_gz = (int32_t)s16(raw[12], raw[13]) * 16 / 131;
  return true;
}

// Compute |accel| as a long (avoids overflow of int16 squares).
static long accelMag(int16_t ax, int16_t ay, int16_t az) {
  long x = ax, y = ay, z = az;
  return (long)sqrtf((float)(x*x + y*y + z*z));
}

// Load calibration values from NVS (called in setup after imuInit)
void loadImuCal() {
  prefs.begin("otto", true);
  imuCalAx    = (int16_t)prefs.getInt("icalax", 0);
  imuCalAy    = (int16_t)prefs.getInt("icalay", 0);
  imuCalAz    = (int16_t)prefs.getInt("icalaz", 981);   // 1 g = 981 UI units
  imuCalGx    = (int16_t)prefs.getInt("icalgx", 0);
  imuCalGy    = (int16_t)prefs.getInt("icalgy", 0);
  imuCalGz    = (int16_t)prefs.getInt("icalgz", 0);
  imuLiftDelta= prefs.getInt("icaldlt", 240);           // ~0.24 g default
  bool stored = prefs.getBool("icalok", false);
  liftDetectEnabled = prefs.getBool("liften", true);   // feature master switch
  helpMeEnabled     = prefs.getBool("helpme", true);   // scared "help me" cry
  prefs.end();
  imuRestMag  = accelMag(imuCalAx, imuCalAy, imuCalAz);
  accelPitchRoll(imuCalAx, imuCalAy, imuCalAz, &imuCalPitch, &imuCalRoll);

  // Deliberately do NOT trust the stored calibration at boot. The saved
  // baseline is only valid for the pose the robot was calibrated in; if it is
  // powered on lying on its side, a stale baseline reads as "lifted" and the
  // feet shake forever. Require a fresh /imucal after every power-on — until
  // then imuCalValid stays false and lift detection is disabled entirely.
  imuCalValid = false;
  dlogPrintf("imu cal: ax=%d ay=%d az=%d restMag=%ld delta=%d stored=%d "
             "— feet locked until /imucal is run this boot",
             (int)imuCalAx, (int)imuCalAy, (int)imuCalAz,
             imuRestMag, imuLiftDelta, (int)stored);
}

void saveImuCal() {
  prefs.begin("otto", false);
  prefs.putInt("icalax",  (int)imuCalAx);
  prefs.putInt("icalay",  (int)imuCalAy);
  prefs.putInt("icalaz",  (int)imuCalAz);
  prefs.putInt("icalgx",  (int)imuCalGx);
  prefs.putInt("icalgy",  (int)imuCalGy);
  prefs.putInt("icalgz",  (int)imuCalGz);
  prefs.putInt("icaldlt", imuLiftDelta);
  prefs.putBool("icalok", true);
  prefs.putBool("liften", liftDetectEnabled);
  prefs.putBool("helpme", helpMeEnabled);
  prefs.end();
}

// Persist the scared-feature switches (used without a full recal).
void saveLiftCfg() {
  prefs.begin("otto", false);
  prefs.putBool("liften", liftDetectEnabled);
  prefs.putBool("helpme", helpMeEnabled);
  prefs.end();
}

// GET /imucal  — sample IMU_CAL_SAMPLES readings, average them, save.
// Optional: ?thresh=N to override the lift-delta threshold (1000..12000).
// Robot must be stationary and in its normal standing position when called.
// Sample the IMU at rest, average it into the calibration baseline, cache the
// derived rest magnitude / pitch / roll, and persist. Returns false (baseline
// left untouched) if the chip fell off the bus mid-sample. Shared by /imucal
// and the auto-calibrate path of /liftdetect so the two never drift apart.
//
// The gyro is averaged alongside the accel: its zero-rate bias is what makes the
// UI's gx/gy/gz sit at a few hundred LSB when the robot is dead still, and
// subtracting it is half of what makes "Calibrate" actually read zero.
bool imuCalibrateNow(int* outCollected) {
  int32_t sumAx = 0, sumAy = 0, sumAz = 0;
  int32_t sumGx = 0, sumGy = 0, sumGz = 0;
  int collected = 0;
  for (int i = 0; i < IMU_CAL_SAMPLES; i++) {
    if (imuReadRaw()) {
      sumAx += imu_ax; sumAy += imu_ay; sumAz += imu_az;
      sumGx += imu_gx; sumGy += imu_gy; sumGz += imu_gz;
      collected++;
    }
    delay(IMU_PERIOD_MS);
    yield();
  }
  if (outCollected) *outCollected = collected;
  if (collected < 10) {
    // The chip dropped off the bus mid-calibration. Leave the previous baseline
    // alone rather than saving an average of nothing.
    imuFailStreak = IMU_FAIL_LIMIT;   // let loop() take it offline promptly
    return false;
  }
  imuCalAx   = (int16_t)(sumAx / collected);
  imuCalAy   = (int16_t)(sumAy / collected);
  imuCalAz   = (int16_t)(sumAz / collected);
  imuCalGx   = (int16_t)(sumGx / collected);
  imuCalGy   = (int16_t)(sumGy / collected);
  imuCalGz   = (int16_t)(sumGz / collected);
  imuCalValid= true;
  imuRestMag = accelMag(imuCalAx, imuCalAy, imuCalAz);
  // Remember the pose we were calibrated in, so pitch/roll can be reported as a
  // deviation from it. This is what parks the UI cube on its neutral face.
  accelPitchRoll(imuCalAx, imuCalAy, imuCalAz, &imuCalPitch, &imuCalRoll);
  saveImuCal();
  dlogPrintf("imucal: a=%d,%d,%d g=%d,%d,%d restMag=%ld pitch=%.1f roll=%.1f "
             "delta=%d (%d samples)",
             (int)imuCalAx, (int)imuCalAy, (int)imuCalAz,
             (int)imuCalGx, (int)imuCalGy, (int)imuCalGz,
             imuRestMag, imuCalPitch, imuCalRoll, imuLiftDelta, collected);
  return true;
}

void handleImuCal() {
  if (!imuReady) { server.send(503, "text/plain", "IMU not ready"); return; }

  // Override threshold if provided
  if (server.hasArg("thresh")) {
    imuLiftDelta = constrain(server.arg("thresh").toInt(), 60, 3000);
  }

  int collected = 0;
  if (!imuCalibrateNow(&collected)) {
    server.send(500, "text/plain", "calibration failed: too few samples");
    return;
  }

  String j = "{\"ok\":1";
  j += ",\"ax\":";      j += (int)imuCalAx;
  j += ",\"ay\":";      j += (int)imuCalAy;
  j += ",\"az\":";      j += (int)imuCalAz;
  j += ",\"gx\":";      j += (int)imuCalGx;
  j += ",\"gy\":";      j += (int)imuCalGy;
  j += ",\"gz\":";      j += (int)imuCalGz;
  j += ",\"restMag\":"; j += imuRestMag;
  j += ",\"delta\":";   j += imuLiftDelta;
  j += ",\"n\":";       j += collected;
  j += "}";
  server.send(200, "application/json", j);
}

// GET /liftdetect?on=0|1  — master switch for the pick-up / shake reaction.
// Turning it ON auto-calibrates (samples the current rest pose) so the feature
// is armed immediately; turning it OFF disarms it without discarding the last
// calibration. Optional ?thresh=N sets the lift sensitivity in the same call.
// With no "on" arg it simply reports the current state.
void handleLiftDetect() {
  if (server.hasArg("thresh"))
    imuLiftDelta = constrain(server.arg("thresh").toInt(), 60, 3000);

  bool calOk = imuCalValid;
  int  n     = 0;

  if (server.hasArg("on")) {
    liftDetectEnabled = (server.arg("on").toInt() != 0);
    saveLiftCfg();
    // Auto-calibrate on enable so the switch is "armed" the instant it flips on.
    if (liftDetectEnabled) calOk = imuReady ? imuCalibrateNow(&n) : false;
  }

  String j = "{\"enabled\":"; j += liftDetectEnabled ? 1 : 0;
  j += ",\"calOk\":";   j += calOk ? 1 : 0;
  j += ",\"n\":";       j += n;
  j += ",\"restMag\":"; j += imuRestMag;
  j += ",\"thresh\":";  j += imuLiftDelta;
  j += ",\"imu\":";     j += imuReady ? 1 : 0;
  j += "}";
  server.send(200, "application/json", j);
  dlogPrintf("liftdetect: %s (calOk=%d thresh=%d)",
             liftDetectEnabled ? "ENABLED" : "disabled", calOk ? 1 : 0, imuLiftDelta);
}

// GET /helpme?on=0|1 — toggle the scared "help me" cry (played at full volume
// 1 s after the scared face appears). ?test=1 plays it once now. No arg = report.
void handleHelpMe() {
  if (server.hasArg("on")) {
    helpMeEnabled = (server.arg("on").toInt() != 0);
    saveLiftCfg();
    dlogPrintf("helpme: %s", helpMeEnabled ? "ENABLED" : "disabled");
  }
  bool doTest = server.hasArg("test") && server.arg("test").toInt() != 0;

  String j = "{\"enabled\":"; j += helpMeEnabled ? 1 : 0;
  j += ",\"tested\":"; j += doTest ? 1 : 0;
  j += "}";
  server.send(200, "application/json", j);

  // Reply first, then play (the cry blocks for ~1.5 s).
  if (doTest) { dlogPrintf("helpme: test play"); playHelpMe(); }
}

// GET /recover — universal screen/face recovery. One button for "the panel went
// blank / garbled". It aborts whatever mode owned the screen (dance / listen /
// scared lift-reaction), re-initialises the ST7735 from scratch (clears any
// blank / white / sleep / display-off / corrupted-register state that a plain
// GRAM repaint cannot fix), parks the legs, and repaints the default Normal
// face. Works as long as the web server still answers — i.e. loop() is alive.
void handleRecover() {
  motionDisarm("screen recovery");
  imuCalValid=false;
  dlogPrintf("recover: requested — aborting modes, re-initialising TFT");

  // 1. Drop every mode that repaints the face, so nothing draws over the reset.
  danceMode       = false;
  scaredMode      = false;
  listenUntil     = 0;
  reactState      = RS_IDLE;   // abort a stuck lift reaction
  liftHitCount    = 0;
  liftWindowStart = 0;
  imuLifted       = false;
  expression      = 6;         // default Normal face

  // 2. Full controller re-init — the heavy hammer a power-cycle would apply.
  tft.initR(INITR_BLACKTAB);
  tft.setSPISpeed(8000000);
  tft.setRotation(1);

  // 3. Park the legs (a reaction aborted mid-flip leaves them splayed).
  standNeutral();

  // 4. Force a clean full repaint of the Normal face right now.
  lastFaceId = 255;
  prevExpr   = 255;
  drawFaceNow();

  server.send(200, "text/plain", "recovered → Normal face");
  dlogPrintf("recover: done → Normal face (heap=%u)", ESP.getFreeHeap());
}

// GET /imu — live IMU state as JSON.
//
// Reports TWO views of every axis:
//   ax/ay/az, gx/gy/gz  — raw sensor counts, for debugging wiring
//   rax/ray/raz, rgx/…  — the same values with the calibration baseline
//                          subtracted, i.e. ZERO at rest once calibrated
// plus pitch/roll already expressed as a deviation from the calibrated pose.
//
// The relative view is what the UI displays. Pressing Calibrate therefore drops
// every number on the panel to ~0 and returns the 3D cube to its neutral face,
// whatever angle the IMU happens to be glued on at.
void handleImu() {
  // current accel magnitude
  long mag = accelMag(imu_ax, imu_ay, imu_az);
  long delta = labs(mag - imuRestMag);

  // Zero-referenced axes. Before the first calibration of this boot there is no
  // trustworthy baseline, so the relative view falls back to raw rather than
  // silently subtracting a stale NVS value and showing a confident wrong zero.
  int rax = imu_ax, ray = imu_ay, raz = imu_az;
  int rgx = imu_gx, rgy = imu_gy, rgz = imu_gz;
  float pitch = 0.0f, roll = 0.0f;
  accelPitchRoll(imu_ax, imu_ay, imu_az, &pitch, &roll);
  if (imuCalValid) {
    rax = imu_ax - imuCalAx;  ray = imu_ay - imuCalAy;  raz = imu_az - imuCalAz;
    rgx = imu_gx - imuCalGx;  rgy = imu_gy - imuCalGy;  rgz = imu_gz - imuCalGz;
    pitch = angleDelta(pitch, imuCalPitch);
    roll  = angleDelta(roll,  imuCalRoll);
  }

  String j = "{\"ax\":"; j += imu_ax;
  j += ",\"ay\":";       j += imu_ay;
  j += ",\"az\":";       j += imu_az;
  j += ",\"gx\":";       j += imu_gx;
  j += ",\"gy\":";       j += imu_gy;
  j += ",\"gz\":";       j += imu_gz;
  j += ",\"rax\":";      j += rax;
  j += ",\"ray\":";      j += ray;
  j += ",\"raz\":";      j += raz;
  j += ",\"rgx\":";      j += rgx;
  j += ",\"rgy\":";      j += rgy;
  j += ",\"rgz\":";      j += rgz;
  j += ",\"gmagPeak\":"; j += imuGmagPeak;   // peak gyro-magnitude since last read
  j += ",\"shakeThr\":"; j += imuShakeGyro;  // gyro threshold that triggers a shake
  imuGmagPeak = 0;                            // reset the peak-hold for the next window
  j += ",\"pitch\":";    j += String(pitch, 1);
  j += ",\"roll\":";     j += String(roll, 1);
  j += ",\"lift\":";     j += imuLifted ? 1 : 0;
  j += ",\"ok\":";       j += imuReady  ? 1 : 0;
  j += ",\"chip\":\"";   j += imuWho ? imuChipName() : "none"; j += "\"";
  j += ",\"addr\":";     j += imuAddr;
  j += ",\"whoAmI\":";   j += imuWho;
  j += ",\"calOk\":";    j += imuCalValid ? 1 : 0;
  j += ",\"calAx\":";    j += (int)imuCalAx;
  j += ",\"calAy\":";    j += (int)imuCalAy;
  j += ",\"calAz\":";    j += (int)imuCalAz;
  j += ",\"restMag\":";  j += imuRestMag;
  j += ",\"mag\":";      j += mag;
  j += ",\"delta\":";    j += delta;
  j += ",\"thresh\":";   j += imuLiftDelta;
  j += ",\"liftEn\":";   j += liftDetectEnabled ? 1 : 0;
  // Reaction phase: 0=idle 1=flipping 2=settling 3=recalibrating.
  // Lets the panel explain why the numbers are jumping around instead of
  // looking like it has glitched.
  j += ",\"react\":";      j += (int)reactState;
  // Link health — lets the UI say "reconnecting" instead of just going blank.
  j += ",\"fails\":";      j += imuFailStreak;
  j += ",\"reconnects\":"; j += (unsigned long)imuReconnects;
  j += ",\"lastOkMs\":";   j += (unsigned long)(imuLastOkMs ? millis() - imuLastOkMs : 0);
  j += "}";
  server.send(200, "application/json", j);
}

// ── Lift reaction: scared face → foot flips → auto-recalibrate ──
//
// Picking the robot up (or shaking it hard) runs a fixed routine, exactly like
// a dance move: the feet swing out to REACT_ANGLE, come back, and repeat
// REACT_FLIPS times, with the scared face up for the whole thing. When the legs
// stop, the robot re-learns whatever pose it is now sitting in and returns the
// face to whatever it was before.
//
// THE RULE THIS FILE LIVES BY: nothing here blocks. Every phase is a state
// machine driven from loop(), so server.handleClient() and screenTick() keep
// running at full rate throughout. The previous design was already non-blocking
// in the servos, but it re-evaluated the IMU the entire time, which meant a fast
// wobble could re-trigger and re-arm the reaction over and over. Now the IMU is
// ignored outright until the routine is finished — see the RS_* gate in loop().
//
// Phases:
//   RS_FLIP   feet run the waypoint sequence          (~REACT_FLIP_MS)
//   RS_SETTLE legs parked, wait for vibration to die  (REACT_SETTLE_MS)
//   RS_RECAL  average fresh samples, adopt as the new baseline
//   RS_IDLE   normal lift detection is armed again
#define REACT_FLIPS            8    // out-and-back cycles per lift (scared: 8 kicks)
#define REACT_ANGLE           25    // ° each foot swings from neutral
#define REACT_LEG_MS         240    // one waypoint-to-waypoint move
#define REACT_SETTLE_MS      700    // let the chassis stop ringing before sampling
#define REACT_RECAL_SAMPLES   20    // ~1 s of readings at IMU_PERIOD_MS
#define REACT_RECAL_TIMEOUT 4000    // give up if the IMU stops answering mid-recal

// One segment per waypoint hop. Segments 1..2*FLIPS are the flips themselves;
// segment 0 leaves neutral and the last one returns to it, so the routine always
// starts and ends at rest with no snap at either end.
#define REACT_SEGMENTS  (2 * REACT_FLIPS + 1)
#define REACT_FLIP_MS   (REACT_SEGMENTS * REACT_LEG_MS)

// (enum ReactState and `reactState` itself are declared with the IMU state
//  further up, so that handleImu() can report the current phase.)
static unsigned long reactStart     = 0;   // millis() when the current phase began
static unsigned long nextReactFrame = 0;   // next servo frame due
static int32_t       recalSum[6]    = {0,0,0,0,0,0};
static int           recalCount     = 0;

// Foot deviation at waypoint k, in degrees from neutral.
// k = 0 and k = REACT_SEGMENTS are neutral; everything between alternates.
static int reactWaypoint(int k) {
  if (k <= 0 || k >= REACT_SEGMENTS) return 0;
  return (k & 1) ? REACT_ANGLE : -REACT_ANGLE;
}

// Called once the 4-hit window is satisfied. Shows scared face + legs,
// then endLiftReaction() will set expression=Angry when legs finish.
void startLiftReaction() {
  reactState     = RS_FLIP;
  reactStart     = millis();
  nextReactFrame = 0;
  scaredMode     = true;
  feetFlipping   = true;   // suspend display animation for the whole slew
  scaredFrame    = 0;
  scaredSweatOff = 0;
  imuLifted      = true;

  // Paint the scared face RIGHT NOW, while the feet are still parked and the SPI
  // bus is quiet — reactTickFeet() only starts slewing the servos later this same
  // loop pass. Drawing it here keeps the ST7735 registers from being corrupted.
  // prevExpr is set equal to expression so screenTick does NOT repaint it during
  // the flips; the animation resumes on its own once feetFlipping clears.
  drawFaceNow();
  prevExpr = expression;
  dlogPrintf("react: 4 hits in window → scared+legs (drawn quiet)");

  // Play HelpmeSound.wav immediately after the face draws, while the SPI bus is
  // still completely quiet and before reactTickFeet() has moved a single servo.
  // Being blocking here is the KEY: by the time we return the audio is done,
  // the feet start moving on the NEXT loop() pass, so I2S audio and SPI/servos
  // never overlap — exactly the same isolation pattern used for the face draw.
  if (helpMeEnabled) {
    dlogPrintf("help-me: playing at max vol (bus quiet, feet not yet moving)");
    playHelpMe();
  }

  // Reset the reaction timer AFTER audio finishes so reactTickFeet() counts
  // elapsed time from now, not from before the ~4 s blocking audio call.
  reactStart    = millis();
  nextReactFrame = 0;
  helpMePlayed  = true;   // already played — loop() guard does nothing
  scaredDrawnAt = millis();
}

static void endLiftReaction(const char* why) {
  reactState      = RS_IDLE;
  scaredMode      = false;
  feetFlipping    = false;
  liftHitCount    = 0;
  liftWindowStart = 0;
  imuLifted       = false;
  // Re-assert the rotation/MADCTL register before the Angry face repaints. If a
  // stray bit of noise DID corrupt it during the one scared paint, this puts it
  // right on a now-quiet bus, so the robot always lands on a clean Angry face
  // without needing the manual Recover button. Cheap, no flash, no GRAM touched.
  tft.setRotation(1);
  // Go straight to Angry.
  expression = 2;
  prevExpr   = 255;
  fireInit   = false;
  dlogPrintf("react: done (%s) → ANGRY", why);
}

// Servo half of the routine. Position is a pure function of elapsed time, so
// there is no per-frame state to drift and a missed frame simply corrects
// itself on the next one.
void reactTickFeet() {
  if (reactState != RS_FLIP) return;
  unsigned long now = millis();
  unsigned long el  = now - reactStart;

  if (el >= (unsigned long)REACT_FLIP_MS) {
    setServoSmooth(CH_LF, 90);
    setServoSmooth(CH_RF, 90);
    reactState   = RS_SETTLE;
    feetFlipping = false;   // feet parked → safe to animate the face again
    reactStart   = now;
    return;
  }
  if (now < nextReactFrame) return;
  nextReactFrame = now + SERVO_FRAME_MS;

  int   seg  = (int)(el / REACT_LEG_MS);
  float frac = (float)(el % REACT_LEG_MS) / (float)REACT_LEG_MS;
  // Cosine ease so each hop starts and ends at zero velocity. Back-to-back
  // linear moves reverse direction instantly at the waypoint, and the servo
  // answers that with a current spike and an audible snap.
  float e = 0.5f - 0.5f * cosf(PI * frac);

  int a = reactWaypoint(seg);
  int b = reactWaypoint(seg + 1);
  int d = a + (int)lroundf((b - a) * e);

  setServoSmooth(CH_LF, 90 + d);
  setServoSmooth(CH_RF, 90 - d);
}


// =============================================================
//  Camera — OV2640 via CSI/DVP (ESP32-S3-N16R8)
// =============================================================
bool camReady = false;   // set true only if esp_camera_init() succeeds

void cameraInit() {
  camera_config_t cfg = {};   // zero every field we don't set (SCCB port etc.)
  // LEDC timer 3 / channel 7 for the 20 MHz XCLK. Timer 0 / channel 0 is the
  // library default, but the servos use LEDC channels 0-3 (timers 0-1) — the
  // camera would retune timer 0 to 20 MHz and the hips would lose their pulse.
  cfg.ledc_channel  = LEDC_CHANNEL_7;
  cfg.ledc_timer    = LEDC_TIMER_3;
  cfg.pin_d0        = Y2_GPIO_NUM;
  cfg.pin_d1        = Y3_GPIO_NUM;
  cfg.pin_d2        = Y4_GPIO_NUM;
  cfg.pin_d3        = Y5_GPIO_NUM;
  cfg.pin_d4        = Y6_GPIO_NUM;
  cfg.pin_d5        = Y7_GPIO_NUM;
  cfg.pin_d6        = Y8_GPIO_NUM;
  cfg.pin_d7        = Y9_GPIO_NUM;
  cfg.pin_xclk      = XCLK_GPIO_NUM;
  cfg.pin_pclk      = PCLK_GPIO_NUM;
  cfg.pin_vsync     = VSYNC_GPIO_NUM;
  cfg.pin_href      = HREF_GPIO_NUM;
  cfg.pin_sccb_sda  = SIOD_GPIO_NUM;
  cfg.pin_sccb_scl  = SIOC_GPIO_NUM;
  cfg.pin_pwdn      = PWDN_GPIO_NUM;
  cfg.pin_reset     = RESET_GPIO_NUM;
  cfg.xclk_freq_hz  = CAM_XCLK_HZ;      // 20 MHz
  cfg.pixel_format  = PIXFORMAT_JPEG;
  // QVGA (320x240) keeps memory pressure low while audio + WiFi are running.
  cfg.frame_size    = FRAMESIZE_QVGA;
  cfg.grab_mode     = CAMERA_GRAB_LATEST;   // always serve the newest frame
  bool psram = psramFound();
  if (psram) {
    cfg.jpeg_quality = 12;                  // 10 = best, 63 = lowest
    cfg.fb_count     = 2;                   // double-buffer for smoother MJPEG
    cfg.fb_location  = CAMERA_FB_IN_PSRAM;
  } else {
    // No PSRAM enabled (Tools > PSRAM = Disabled): one smaller JPEG buffer in
    // internal RAM still gives a working live view.
    cfg.jpeg_quality = 15;
    cfg.fb_count     = 1;
    cfg.fb_location  = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    dlogPrintf("cam: init FAILED 0x%x — camera endpoints will return 503", err);
    camReady = false;
    if (err == ESP_ERR_NOT_FOUND || err == 0x20001 /* ESP_ERR_CAMERA_NOT_DETECTED */)
      hwSet(HW_CAMERA, HW_FAIL, "not detected (0x%x) - check ribbon cable", err);
    else
      hwSet(HW_CAMERA, HW_FAIL, "init failed 0x%x", err);
    return;
  }
  camReady = true;
  camera_sensor_t* s = esp_camera_sensor_get();
  // This module actually carries an OV3660 (PID 0x3660). Out of the box it is
  // flipped and comes out dark and green-tinted; these are the corrections
  // Espressif's own CameraWebServer example applies for that sensor.
  if (s && s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  dlogPrintf("cam: ready PID=0x%x (QVGA JPEG, %s)", s ? s->id.PID : 0,
             psram ? "PSRAM x2" : "DRAM x1");
  hwSet(HW_CAMERA, HW_OK, "sensor 0x%x, QVGA, stream on port 81%s",
        s ? s->id.PID : 0, psram ? "" : " (no PSRAM)");
}

// GET /cam/snapshot — returns a single JPEG frame
void handleCamSnapshot() {
  if (!camReady) { server.send(503, "text/plain", "camera not available"); return; }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { server.send(500, "text/plain", "capture failed"); return; }
  server.sendHeader("Content-Disposition", "inline; filename=snapshot.jpg");
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// ── Live stream on its OWN server, port 81 ─────────────────────
// The main WebServer is single-threaded: an endless MJPEG loop inside one of
// its handlers froze the whole robot (no UI, no face, no lift detection) for
// as long as anyone watched. esp_http_server runs in its own task, so the
// stream at http://<ip>:81/stream costs the rest of the firmware nothing.
static httpd_handle_t camHttpd = nullptr;

static esp_err_t camStreamHandler(httpd_req_t* req) {
  if (!camReady) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, "camera not available");
  }
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  esp_err_t res = ESP_OK;
  char hdr[64];
  while (res == ESP_OK) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { vTaskDelay(pdMS_TO_TICKS(30)); continue; }
    int hlen = snprintf(hdr, sizeof(hdr),
                        "--frame\r\nContent-Type: image/jpeg\r\n"
                        "Content-Length: %u\r\n\r\n", (unsigned)fb->len);
    res = httpd_resp_send_chunk(req, hdr, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, "\r\n", 2);
    esp_camera_fb_return(fb);
  }
  return res;   // client went away
}

void startCamServer() {
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.server_port = 81;
  c.ctrl_port   = 32769;   // must differ from any other httpd instance
  c.max_open_sockets = 3;
  if (httpd_start(&camHttpd, &c) != ESP_OK) {
    dlogPrintf("cam: stream server on :81 FAILED to start");
    camHttpd = nullptr;
    return;
  }
  httpd_uri_t u = {};
  u.uri = "/stream"; u.method = HTTP_GET; u.handler = camStreamHandler;
  httpd_register_uri_handler(camHttpd, &u);
  dlogPrintf("cam: stream server up — http://<ip>:81/stream");
}

// GET /cam/stream on port 80 — kept for old links; bounces to port 81.
void handleCamStream() {
  String host = server.hostHeader();
  int colon = host.indexOf(':');
  if (colon >= 0) host = host.substring(0, colon);
  server.sendHeader("Location", "http://" + host + ":81/stream");
  server.send(302, "text/plain", "stream moved to port 81");
}


// =============================================================
//  Setup & Loop
// =============================================================
// GET /hw — per-part hardware status for the UI's status card.
void handleHw() {
  String j = "{\"ready\":";
  j += hwInitDone ? 1 : 0;
  j += ",\"step\":\""; j += hwStep; j += "\"";
  j += ",\"uptime\":"; j += millis() / 1000;
  j += ",\"items\":[";
  for (int i = 0; i < HW_COUNT; i++) {
    if (i) j += ",";
    j += "{\"k\":\""; j += hw[i].key;
    j += "\",\"n\":\""; j += hw[i].name;
    j += "\",\"s\":";   j += (int)hw[i].st;
    j += ",\"i\":\"";
    for (const char* p = hw[i].info; *p; p++) {   // JSON-escape quotes/backslashes
      if (*p == '"' || *p == '\\') j += '\\';
      j += *p;
    }
    j += "\"}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

// Handlers that touch hardware answer 503 until hwInitTask has finished, so a
// click during the first seconds after boot can't race the init code.
#include "motion_api.h"
#include "servo_mapping_api.h"

// Rebase all occupied NVS slots from oldNeutral to newNeutral.
// Each absolute angle is shifted by (newNeutral[ch] - oldNeutral[ch]).
// Frames whose rebased angles would fall outside the new joint limits are
// clamped; the slot is skipped (not deleted) if the sequence fails validation.
// Called automatically by handleSavePose after the new neutral is adopted.
static void rebaseAllSlots(const float oldNeutral[4], const bobmotion::Config& newCfg) {
  const float* newNeutral = newCfg.neutral;
  float delta[4];
  bool anyDelta = false;
  for (unsigned ch = 0; ch < 4; ++ch) {
    delta[ch] = newNeutral[ch] - oldNeutral[ch];
    if (fabsf(delta[ch]) > 0.5f) anyDelta = true;
  }
  if (!anyDelta) return;   // neutral unchanged — nothing to do

  for (unsigned slot = 0; slot < bobmotion::SLOTS; ++slot) {
    char key[12]; motionSlotKey(slot, key, sizeof(key));
    cJSON* j = motionLoad(key);
    if (!j) continue;   // empty slot

    cJSON* framesJ = cJSON_GetObjectItemCaseSensitive(j, "frames");
    if (!cJSON_IsArray(framesJ)) { cJSON_Delete(j); continue; }

    // Skip tilt sequences — they store orientation targets, not joint angles
    cJSON* modeJ = cJSON_GetObjectItemCaseSensitive(j, "mode");
    if (cJSON_IsString(modeJ) && strcmp(modeJ->valuestring, "joints") != 0) {
      cJSON_Delete(j); continue;
    }

    bool changed = false;
    int sz = cJSON_GetArraySize(framesJ);
    for (int fi = 0; fi < sz; ++fi) {
      cJSON* f = cJSON_GetArrayItem(framesJ, fi);
      cJSON* anglesJ = cJSON_GetObjectItemCaseSensitive(f, "angles");
      if (!cJSON_IsArray(anglesJ) || cJSON_GetArraySize(anglesJ) != 4) continue;
      for (int ch = 0; ch < 4; ++ch) {
        cJSON* v = cJSON_GetArrayItem(anglesJ, ch);
        if (!cJSON_IsNumber(v)) continue;
        float newVal = bobmotion::bound((float)v->valuedouble + delta[ch], 0.0f, 180.0f);
        v->valuedouble = newVal;
        changed = true;
      }
    }

    if (changed) {
      // Re-parse through motionParseSequence to validate against new limits
      bobmotion::Sequence seq;
      bool valid = motionParseSequence(j, seq);
      if (valid) {
        // Store the rebased sequence back to NVS
        cJSON* stored = motionSequenceJson(seq);
        motionStore(key, stored);   // motionStore deletes stored
        dlogPrintf("savepose: rebased slot %u to new neutral", slot);
      } else {
        dlogPrintf("savepose: slot %u skipped — rebased angles outside new limits", slot);
      }
    }
    cJSON_Delete(j);
  }
}

// GET /savepose[?radius=N][?nocal=1] — "Save Calibrated Position".
// Captures the CURRENT commanded servo pose as the calibrated home/idle pose and
// (unless nocal=1) calibrates the IMU at that pose, persisting BOTH to NVS in one
// action. This is the fix for "restart doesn't save": after this, boot holds this
// pose (motion.config.customNeutral) instead of the 90° centre, and every gait,
// dance and IMU move centres on it via homeAngle(). It never moves a servo — pose
// Bob first with the sliders (or leave him at his assembled rest), then save once.
//
// radius (default 30, 1..45) is the per-joint travel window Motion Studio
// SEQUENCES may use around the pose; legacy gaits/dances are not bounded by it.
// The window is clamped so command+trim always stays within 0..180.
// After the new neutral is committed, all occupied NVS joint-mode slots are
// automatically rebased to the new neutral so saved moves stay correct.
void handleSavePose() {
  if (motion.armed) { server.send(409,"text/plain","stop Motion Studio before saving a pose"); return; }
  for (unsigned i=0;i<4;++i)
    if (servos[i].pin<0) { server.send(503,"text/plain","a servo PWM channel is unavailable"); return; }

  int  radius = server.hasArg("radius") ? constrain((int)server.arg("radius").toInt(),1,45) : 30;
  bool doCal  = !(server.hasArg("nocal") && server.arg("nocal").toInt()!=0);

  bobmotion::Config candidate = motion.config;
  for (unsigned i=0;i<4;++i) {
    float n = constrain((float)servoCur[i], 0.0f, 180.0f);
    // command+trim must stay in [0,180]; derive the legal command window first.
    float lowLimit  = fmaxf(0.0f,   -(float)trim[i]);
    float highLimit = fminf(180.0f, 180.0f-(float)trim[i]);
    n = bobmotion::bound(n, lowLimit, highLimit);
    float low  = fmaxf(lowLimit,  n - radius);
    float high = fminf(highLimit, n + radius);
    if (high - low < 2) { server.send(400,"text/plain","pose too close to a servo limit for the chosen radius"); return; }
    candidate.neutral[i]    = n;
    candidate.joint[i].low  = low;
    candidate.joint[i].high = high;
  }
  candidate.customNeutral = true;
  // A rest pose implies nothing about correction direction. Keep PID gains at
  // zero and conservative limits until the user tunes them in Motion Studio.
  candidate.gains[0] = candidate.gains[1] = bobmotion::Gains{0,0,0};
  if (candidate.correction < 1)  candidate.correction = 8;
  if (candidate.slew < 40)       candidate.slew = 40;   // lively but safe expression speed
  if (!bobmotion::validConfig(candidate)) { server.send(400,"text/plain","captured pose failed validation"); return; }

  // Persist the profile first (never writes a PWM command), then adopt it.
  // Snapshot the old neutral before overwriting so rebaseAllSlots can compute deltas.
  float oldNeutral[4];
  for (unsigned i=0;i<4;++i) oldNeutral[i] = motion.config.customNeutral ? motion.config.neutral[i] : 90.0f;

  if (!motionStore("config", motionConfigJson(candidate))) {
    server.send(507,"text/plain","idle pose could not be saved to NVS"); return;
  }
  motion.configure(candidate);

  // Auto-rebase all occupied NVS joint-mode slots to the new neutral so every
  // saved move stays correct without any manual intervention.
  rebaseAllSlots(oldNeutral, candidate);

  int  collected = 0;
  bool cal = false;
  if (doCal && imuReady) cal = imuCalibrateNow(&collected);

  String j = "{\"ok\":1,\"neutral\":[";
  for (unsigned i=0;i<4;++i){ if(i) j+=","; j+=(int)lroundf(candidate.neutral[i]); }
  j += "],\"radius\":";       j += radius;
  j += ",\"imuCalibrated\":"; j += cal?1:0;
  j += ",\"imuReady\":";      j += imuReady?1:0;
  j += ",\"n\":";             j += collected;
  j += "}";
  server.send(200,"application/json",j);
  dlogPrintf("savepose: neutral=[%d,%d,%d,%d] radius=%d imucal=%d",
             (int)lroundf(candidate.neutral[0]),(int)lroundf(candidate.neutral[1]),
             (int)lroundf(candidate.neutral[2]),(int)lroundf(candidate.neutral[3]), radius, cal?1:0);
}

// GET /playslot?n=N — play saved movement slot N as a smooth, blocking on-device
// sequence (no token/heartbeat), so the dashboard can trigger a saved move with
// one click, exactly like a dance. Joint-mode slots only; tilt/posture slots need
// Motion Studio + PID. Returns 200 immediately, then glides through the keyframes.
void handlePlaySlot() {
  if (motion.armed) { server.send(409,"text/plain","stop Motion Studio first"); return; }
  int slot = server.hasArg("n") ? server.arg("n").toInt() : -1;
  if (slot<0 || slot>=(int)bobmotion::SLOTS) { server.send(400,"text/plain","bad slot"); return; }
  char key[12]; motionSlotKey(slot,key,sizeof(key));
  cJSON* j = motionLoad(key);
  if (!j) { server.send(404,"text/plain","slot empty"); return; }
  cJSON* modej = cJSON_GetObjectItemCaseSensitive(j,"mode");
  if (cJSON_IsString(modej) && !strcmp(modej->valuestring,"tilt")) {
    cJSON_Delete(j); server.send(409,"text/plain","posture (tilt) moves need Motion Studio + PID"); return;
  }
  cJSON* frames = cJSON_GetObjectItemCaseSensitive(j,"frames");
  if (!cJSON_IsArray(frames) || cJSON_GetArraySize(frames)<1) { cJSON_Delete(j); server.send(400,"text/plain","no frames"); return; }
  server.send(200,"text/plain","ok");   // reply before the blocking playback

  cJSON* ex = cJSON_GetObjectItemCaseSensitive(j,"expression");
  if (cJSON_IsNumber(ex) && ex->valuedouble>=0 && ex->valuedouble<=6) {
    expression=(uint8_t)ex->valuedouble; danceMode=false; prevExpr=255; drawFaceNow();
  }
  int n = cJSON_GetArraySize(frames);
  for (int i=0;i<n;i++) {
    cJSON* f = cJSON_GetArrayItem(frames,i);
    cJSON* a = cJSON_GetObjectItemCaseSensitive(f,"angles");
    if (!cJSON_IsArray(a) || cJSON_GetArraySize(a)!=4) continue;
    int ang[4];
    for (int c=0;c<4;c++) { cJSON* v=cJSON_GetArrayItem(a,c); ang[c]=constrain(cJSON_IsNumber(v)?(int)lroundf(v->valuedouble):90, 0, 180); }
    cJSON* mvj=cJSON_GetObjectItemCaseSensitive(f,"moveMs"); cJSON* hdj=cJSON_GetObjectItemCaseSensitive(f,"holdMs");
    int mv=constrain(cJSON_IsNumber(mvj)?(int)mvj->valuedouble:500,100,5000);
    int hd=constrain(cJSON_IsNumber(hdj)?(int)hdj->valuedouble:0,0,5000);
    glideTo(ang[0],ang[1],ang[2],ang[3], mv);
    if (hd) delay(hd);
    yield();
  }
  cJSON_Delete(j);
  delay(30); needFullRedraw = true;
  dlogPrintf("playslot: %d done", slot);
}

// GET /movelist — JSON list of the built-in move library (name per index).
void handleMoveList() {
  String j="[";
  for (int i=0;i<BUILTIN_MOVE_COUNT;i++){
    if (i) j+=",";
    j+="{\"n\":"; j+=i; j+=",\"name\":\""; j+=BUILTIN_MOVES[i].name; j+="\"}";
  }
  j+="]";
  server.send(200,"application/json",j);
}

// GET /playmove?n=N — play built-in library move N as a smooth blocking on-device
// sequence (no token/heartbeat). Curl-friendly, so idle mode can trigger any of
// the whole move library, not just the 12 NVS slots.
void handlePlayMove() {
  if (motion.armed) { server.send(409,"text/plain","stop Motion Studio first"); return; }
  int n = server.hasArg("n") ? server.arg("n").toInt() : -1;
  if (n<0 || n>=BUILTIN_MOVE_COUNT) { server.send(400,"text/plain","bad move index"); return; }
  const BuiltinMove& m = BUILTIN_MOVES[n];
  server.send(200,"text/plain","ok");   // reply before the blocking playback
  if (m.expression>=0 && m.expression<=6) { expression=(uint8_t)m.expression; danceMode=false; prevExpr=255; drawFaceNow(); }
  for (uint8_t i=0;i<m.nframes;i++) {
    const BuiltinFrame& f = m.frames[i];
    glideTo(f.a0,f.a1,f.a2,f.a3, f.moveMs);
    if (f.holdMs) delay(f.holdMs);
    yield();
  }
  delay(30); needFullRedraw=true;
  dlogPrintf("playmove: %d (%s) done", n, m.name);
}

static WebServer::THandlerFunction afterInit(WebServer::THandlerFunction h, bool profileAllowed = false, bool motionAllowed = false) {
  return [h, profileAllowed, motionAllowed]() {
    if (BOB_VISUAL_BRINGUP && !profileAllowed) {
      server.send(503, "text/plain", "Peripheral disabled in current bring-up profile.");
      return;
    }
    if (!hwInitDone) {
      server.send(503, "text/plain", hwInitError ? hwInitError : "starting up - hardware still initialising");
      return;
    }
    // Read-only polling (e.g. /servopos) must not dismiss the boot splash.
    const String uri = server.uri();
    // Motion Studio owns the servos while armed. Block legacy motor writes and
    // long audio/calibration operations, including calls from the Mac server.
    const bool readOnly = (uri=="/servomap" && server.method()==HTTP_GET) || uri=="/imu" || uri=="/imupose" ||
      (uri=="/servopos" && !server.hasArg("ch") && !server.hasArg("deg")) ||
      ((uri=="/liftdetect" || uri=="/helpme") && server.args()==0);
    const bool stop = uri=="/recover" || (uri=="/move" &&
      (server.arg("cmd")=="stand" || server.arg("cmd")=="stop"));
    // /mactts is allowed DURING the eyes demo so the Mac can speak scripted lines
    // over the liquid-eyes animation. It touches only the speaker, not the TFT,
    // and handleMacTTSData() freezes the demo clock while audio plays so the eyes
    // hold their phase instead of skipping ahead.
    if (eyeDemoActive && uri!="/eyes/demo" && uri!="/mactts" && !readOnly && !stop) {
      server.send(409,"text/plain","Eyes demo owns the screen; stop it first"); return;
    }
    if (motion.armed && !motionAllowed && !readOnly && !stop) {
      server.send(409,"text/plain","Motion Studio is armed; stop it before using other hardware controls.");
      return;
    }
    if (uri == "/move" || uri == "/eyes" || uri == "/ticker" ||
        uri == "/recover" || uri == "/rps" || uri.startsWith("/rps/") ||
        uri.startsWith("/screen/")) {
      netScreenUntil = 0;
      prevExpr = 255;
    }
    h();
  };
}
static WebServer::THandlerFunction afterInitUpload(WebServer::THandlerFunction h, bool profileAllowed = false) {
  // NB: no !eyeDemoActive gate here — uploads (/mactts speech, /upload) only
  // touch the speaker/flash, never the TFT, so they are safe to run while the
  // liquid-eyes demo animates. handleMacTTSData() freezes the demo clock during
  // playback so the eyes resume on the same phase.
  return [h, profileAllowed]() { if (hwInitDone && !motion.armed && (!BOB_VISUAL_BRINGUP || profileAllowed)) h(); };
}

// =============================================================
//  OTA (Over-The-Air) firmware update
//  Two methods:
//    1. ArduinoOTA (port 3232) — used by arduino-cli, the Arduino IDE
//       "Upload via Wi-Fi" option, and any tool that speaks the
//       espota protocol.  No HTTP browser needed.
//    2. HTTP POST /firmware — curl or browser drag-drop. Sends the
//       compiled .bin directly; the robot reboots when done.
//
//  IMPORTANT — partition scheme:
//    The default "Huge APP (3MB No OTA / 1MB SPIFFS)" has no OTA
//    partition and Update.begin() returns false. You MUST change to
//    a scheme that reserves two app slots before this will work.
//    Recommended: Tools → Partition Scheme → "Minimal SPIFFS (1.9MB
//    APP with OTA / 190KB SPIFFS)" or "Default 4MB with spiffs (1.2MB
//    APP / 1.5MB SPIFFS)".  With 16 MB flash, custom_16MB_ota.csv
//    can give you two 3 MB app slots + 2 MB SPIFFS.
//    After the scheme change, re-upload via USB once; OTA works for
//    every upload afterwards.
// =============================================================

// Password for ArduinoOTA and HTTP /firmware. Set OTA_PASSWORD in NVS
// via the browser UI at /ota, or leave blank to use "bob12345".
static String otaPassword = "bob12345";

void otaSetup() {
  // ── ArduinoOTA ─────────────────────────────────────────────
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname("anvyxo-robo");
  ArduinoOTA.setPassword(otaPassword.c_str());

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "SPIFFS";
    dlogPrintf("OTA: start uploading %s", type.c_str());
    // Stop active audio/servos before the update so I2S DMA and LEDC
    // don't write to hardware while the flash driver is erasing.
    i2s_zero_dma_buffer(I2S_SPK_NUM);
    expression = 6; drawFaceNow();   // normal face
  });
  ArduinoOTA.onEnd([]() {
    dlogPrintf("OTA: upload done — rebooting");
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static unsigned int lastPct = 0;
    unsigned int pct = done * 100 / total;
    if (pct / 10 != lastPct / 10) {
      dlogPrintf("OTA: %u%%", pct);
      lastPct = pct;
    }
  });
  ArduinoOTA.onError([](ota_error_t err) {
    const char* reason = "unknown";
    if      (err == OTA_AUTH_ERROR)    reason = "auth failed — check OTA_PASSWORD";
    else if (err == OTA_BEGIN_ERROR)   reason = "begin failed — wrong partition scheme?";
    else if (err == OTA_CONNECT_ERROR) reason = "connect failed";
    else if (err == OTA_RECEIVE_ERROR) reason = "receive error";
    else if (err == OTA_END_ERROR)     reason = "end error";
    dlogPrintf("OTA error [%u]: %s", (unsigned)err, reason);
  });
  ArduinoOTA.begin();
  dlogPrintf("OTA: ArduinoOTA ready on port 3232 (password: %s)",
             otaPassword.length() ? "***set***" : "none");
}

// ── HTTP /firmware — POST a raw .bin to update firmware ────────────────────
// curl usage (from Mac, replace IP and path):
//   curl -X POST http://192.168.1.15/firmware \
//        -H "X-OTA-Password: bob12345" \
//        -F "firmware=@/path/to/Otto_ESP32S3.ino.bin" --progress-bar
//
// The handler checks the X-OTA-Password header before writing anything.
// Uses the WebServer upload API so memory usage is O(chunk) not O(binary).
// ==========================================================================
static bool   firmwareOtaOk  = false;
static String firmwareOtaErr = "";

void handleFirmwareUploadData() {
  HTTPUpload& up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    firmwareOtaOk  = false;
    firmwareOtaErr = "";
    // Check password in the header before touching flash
    String pwd = server.header("X-OTA-Password");
    if (otaPassword.length() && pwd != otaPassword) {
      firmwareOtaErr = "wrong OTA password";
      dlogPrintf("OTA /firmware: auth failed");
      return;
    }
    dlogPrintf("OTA /firmware: start '%s' (heap=%u)", up.filename.c_str(), ESP.getFreeHeap());
    // U_FLASH = app partition; size 0 = auto-detect from partition table
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      firmwareOtaErr = "Update.begin failed — wrong partition scheme?";
      dlogPrintf("OTA /firmware: %s", firmwareOtaErr.c_str());
    }
    return;
  }

  if (up.status == UPLOAD_FILE_WRITE) {
    if (firmwareOtaErr.length()) return;  // already failed; drain bytes
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      firmwareOtaErr = "flash write failed";
      dlogPrintf("OTA /firmware: write error at %u", (unsigned)up.totalSize);
    }
    return;
  }

  if (up.status == UPLOAD_FILE_END) {
    if (firmwareOtaErr.length()) { Update.abort(); return; }
    if (Update.end(true)) {
      firmwareOtaOk = true;
      dlogPrintf("OTA /firmware: success (%u bytes) — rebooting in 2 s", (unsigned)up.totalSize);
    } else {
      firmwareOtaErr = "Update.end failed";
      dlogPrintf("OTA /firmware: %s", firmwareOtaErr.c_str());
    }
  }
}

void handleFirmwareUploadDone() {
  if (firmwareOtaOk) {
    server.send(200, "application/json",
      "{\"ok\":true,\"msg\":\"Firmware flashed. Rebooting now.\"}");
    delay(500);
    ESP.restart();
  } else {
    server.send(500, "application/json",
      String("{\"ok\":false,\"msg\":\"") + firmwareOtaErr + "\"}");
  }
}

// ── /ota — browser page to read status and set OTA password ────────────────
void handleOtaPage() {
  String ip = homeIP.length() ? homeIP : ottoIP;
  String html = R"rawhtml(<!DOCTYPE html><html><head>
<meta charset='utf-8'><title>Anvyxo OTA</title>
<style>body{font-family:monospace;max-width:520px;margin:40px auto;padding:0 16px;
background:#111;color:#e0e0e0}h2{color:#7ec8e3}
label{display:block;margin:8px 0 4px}
input[type=text],input[type=password]{width:100%;padding:6px;background:#222;color:#e0e0e0;
border:1px solid #444;border-radius:4px;font-family:monospace}
input[type=file]{color:#e0e0e0;margin:8px 0}
button{background:#2a6e9c;color:#fff;border:none;padding:8px 18px;border-radius:4px;
cursor:pointer;margin-top:8px}button:hover{background:#3588bd}
.note{background:#1a2a1a;border:1px solid #3a5a3a;border-radius:4px;padding:12px;
margin:16px 0;color:#90ee90;font-size:12px}
.warn{background:#2a1a1a;border:1px solid #5a3a3a;border-radius:4px;padding:10px;
margin:8px 0;color:#ff9966;font-size:12px}
#status{margin-top:12px;padding:8px;border-radius:4px;display:none}
</style></head><body>
<h2>&#x1F916; Anvyxo OTA</h2>
<div class='note'>
<b>Method 1 — arduino-cli / Arduino IDE "Upload via WiFi"</b><br>
Port: <b>anvyxo-robo.local:3232</b> &nbsp; (or )rawhtml" + ip + R"rawhtml(:3232)<br>
Password: same as below (default: bob12345)
</div>
<div class='note'>
<b>Method 2 — curl (from terminal on Mac/Linux)</b><br>
<code>curl -X POST http://)rawhtml" + ip + R"rawhtml(/firmware \<br>
&nbsp;&nbsp;-H "X-OTA-Password: bob12345" \<br>
&nbsp;&nbsp;-F "firmware=@Otto_ESP32S3.ino.bin"</code>
</div>
<div class='warn'>
<b>Partition warning:</b> OTA requires a partition scheme with two app slots.<br>
If using "Huge APP (3MB No OTA)", run <b>Build → Upload via USB once</b> after
changing the scheme in Arduino IDE → Tools → Partition Scheme → "Minimal SPIFFS
(1.9MB APP with OTA / 190KB SPIFFS)" or a custom 16MB table.
</div>
<hr>
<h3>Upload firmware binary (.bin)</h3>
<label>OTA password (sent as X-OTA-Password header):</label>
<input type='password' id='pwd' value='bob12345'>
<label>Firmware .bin file:</label>
<input type='file' id='bin' accept='.bin'>
<button onclick='doUpload()'>Upload &amp; Reboot</button>
<div id='status'></div>
<script>
function doUpload(){
  var f=document.getElementById('bin').files[0];
  var p=document.getElementById('pwd').value;
  var s=document.getElementById('status');
  if(!f){s.style.display='block';s.style.background='#2a1a1a';s.style.color='#ff9966';s.textContent='Select a .bin file first.';return;}
  s.style.display='block';s.style.background='#1a2a3a';s.style.color='#7ec8e3';
  s.textContent='Uploading '+f.name+' ('+Math.round(f.size/1024)+' KB)…';
  var fd=new FormData();fd.append('firmware',f,f.name);
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/firmware');
  xhr.setRequestHeader('X-OTA-Password',p);
  xhr.upload.onprogress=function(e){if(e.lengthComputable)s.textContent='Uploading… '+Math.round(e.loaded*100/e.total)+'%';};
  xhr.onload=function(){
    try{var r=JSON.parse(xhr.responseText);
      s.style.background=r.ok?'#1a2a1a':'#2a1a1a';
      s.style.color=r.ok?'#90ee90':'#ff9966';
      s.textContent=r.msg;}
    catch(e){s.textContent=xhr.responseText;}
  };
  xhr.onerror=function(){s.style.background='#2a1a1a';s.style.color='#ff9966';s.textContent='Upload failed. Check WiFi.';};
  xhr.send(fd);
}
</script></body></html>)rawhtml";
  server.send(200, "text/html", html);
}

void registerRoutes() {
  // Always available — needed to see what is (not) working.
  server.on("/",           handleRoot);
  server.on("/hw",         handleHw);
  server.on("/state",      handleState);
  server.on("/logs",       handleLogs);
  server.on("/wifistatus", handleWifiStatus);
  server.on("/cam/stream",   HTTP_GET, handleCamStream);    // 302 -> :81/stream
  server.on("/cam/snapshot", HTTP_GET, handleCamSnapshot);  // 503 until camera is up
  server.on("/motion", HTTP_GET, [](){ sendGzipPage(MOTION_PAGE_GZ, MOTION_PAGE_GZ_LEN); });
  server.on("/motion/state", HTTP_GET, afterInit(handleMotionState,true,true));
  server.on("/motion/library", HTTP_GET, afterInit(handleMotionLibrary,true,true));
  server.on("/motion/arm", HTTP_POST, afterInit(handleMotionArm,true,true));
  server.on("/motion/heartbeat", HTTP_POST, afterInit(handleMotionHeartbeat,true,true));
  server.on("/motion/stop", HTTP_POST, afterInit(handleMotionStop,true,true));
  server.on("/motion/setup", HTTP_POST, afterInit(handleMotionSetup,true,true));
  server.on("/motion/config", HTTP_POST, afterInit(handleMotionConfig,true,true));
  server.on("/motion/pid", HTTP_POST, afterInit(handleMotionPid,true,true));
  server.on("/motion/play", HTTP_POST, afterInit(handleMotionPlay,true,true));
  server.on("/motion/save", HTTP_POST, afterInit(handleMotionSave,true,true));
  server.on("/motion/delete", HTTP_POST, afterInit(handleMotionDelete,true,true));

  // Touch hardware — wait for hwInitTask.
  server.on("/rps",        afterInit(handleRps, true));
  server.on("/rps/go",     afterInit(handleRpsGo, true));
  server.on("/rps/reveal", afterInit(handleRpsReveal, true));
  server.on("/move",       afterInit(handleMove, true));
  server.on("/trim",       afterInit(handleTrim, true));
  server.on("/save",       afterInit(handleSave, true));
  server.on("/resettrim",  afterInit(handleResetTrim, true));
  server.on("/assembly",       afterInit(handleAssembly, true));
  server.on("/servomap", HTTP_GET, afterInit(handleServoMapGet, true));
  server.on("/servomap", HTTP_POST, afterInit(handleServoMapSave, true));
  server.on("/servopos",       afterInit(handleServoPos, true));
  server.on("/servocalibrate", afterInit(handleServoCalibrate, true));
  server.on("/servotest",      afterInit(handleServoTest, true));
  server.on("/param",          afterInit(handleParam, true));
  server.on("/eyes/demo", afterInit(handleEyeDemo, true));
  server.on("/eyes",       afterInit(handleEyes, true));
  server.on("/ticker",     afterInit(handleTicker, true));
  server.on("/record",     afterInit(handleRecord, true));
  server.on("/play",       afterInit(handlePlay));
  server.on("/diag",       afterInit(handleDiag, BOB_SPEAKER_TEST));
  server.on("/testtone",   afterInit(handleTestTone, BOB_SPEAKER_TEST));
  server.on("/miclevel",   afterInit(handleMicLevel, true));
  server.on("/micscan",    afterInit(handleMicScan, true));
  server.on("/micconfig",  afterInit(handleMicConfig, true));
  server.on("/files",      afterInit(handleFiles));
  server.on("/playfile",   afterInit(handlePlayFile));
  server.on("/clip",       afterInit(handleClipGet));
  server.on("/delfile",    afterInit(handleDelFile));
  server.on("/recfile",    afterInit(handleRecFile));
  server.on("/imu",        afterInit(handleImu, BOB_IMU_TEST));
  server.on("/imupose",    afterInit(handleImuPose, BOB_IMU_TEST));
  server.on("/imucal",     afterInit(handleImuCal, BOB_IMU_TEST));
  server.on("/savepose",   afterInit(handleSavePose, true));
  server.on("/playslot",   afterInit(handlePlaySlot, true));
  server.on("/movelist",   afterInit(handleMoveList, true));
  server.on("/playmove",   afterInit(handlePlayMove, true));
  server.on("/liftdetect", afterInit(handleLiftDetect, BOB_IMU_TEST));
  server.on("/helpme",     afterInit(handleHelpMe, BOB_IMU_TEST));
  server.on("/recover",    afterInit(handleRecover, true));
  server.on("/wifisave",   HTTP_POST, afterInit(handleWifiSave, true));
  // /screen/<mode> — face context switch from BOB_ROBO_CONVO Python server
  server.on("/screen/idle",   afterInit(handleScreen, true));
  server.on("/screen/listen", afterInit(handleScreen, true));
  server.on("/screen/speak",  afterInit(handleScreen, true));
  server.on("/speak",      HTTP_POST, afterInit(handleSpeak, BOB_SPEAKER_TEST));
  // /mactts is now handled by ttsServer (ESPAsyncWebServer, port 8080).
  // Registered in mttAsyncSetup() called from setup() after server.begin().
  // Upload needs two handlers: the second streams the body to flash,
  // the first replies once the whole file has landed.
  server.on("/upload", HTTP_POST,
            afterInit([](){ server.send(uploadErr.length() ? 500 : 200, "text/plain",
                                        uploadErr.length() ? uploadErr : String("ok")); }),
            afterInitUpload(handleUploadData));
  // Streaming playback — no PSRAM, feeds PCM bytes directly to I2S as they
  // arrive over WiFi. Use TTS_ENDPOINT=streamplay in .env to enable.
  server.on("/streamplay", HTTP_POST,
            afterInit([](){ server.send(200, "text/plain", "ok"); }, BOB_SPEAKER_TEST),
            afterInitUpload(handleStreamData, BOB_SPEAKER_TEST));

  // ── OTA routes ──────────────────────────────────────────────────────────
  // GET  /ota         → browser upload page (drag-drop .bin or show curl command)
  // POST /firmware    → actual binary upload handler; reboots on success
  server.on("/ota", HTTP_GET, handleOtaPage);
  server.on("/firmware", HTTP_POST,
            handleFirmwareUploadDone,
            handleFirmwareUploadData);
  // Tell the WebServer to preserve the X-OTA-Password header so the upload
  // handler can read it via server.header().
  const char* collectH[] = { "X-OTA-Password" };
  server.collectHeaders(collectH, 1);
}

// Quick mic presence test: a dead or unplugged data line reads all-zero or a
// single stuck value; a real mic always has some noise.
void checkMic() {
  static int32_t raw[256];
  size_t br = 0;
  i2s_zero_dma_buffer(I2S_MIC_NUM);
  esp_err_t e = ESP_FAIL;
  for (int k = 0; k < 4; k++)   // skip the first reads while the clock settles
    e = i2s_read(I2S_MIC_NUM, raw, sizeof(raw), &br, pdMS_TO_TICKS(200));
  if (e != ESP_OK || br == 0) { hwSet(HW_MIC, HW_FAIL, "I2S read failed (err %d)", (int)e); return; }
  size_t n = br / sizeof(int32_t);
  int32_t mn = raw[0] >> micShift, mx = mn;
  for (size_t i = 1; i < n; i++) {
    int32_t s = raw[i] >> micShift;
    if (s < mn) mn = s;
    if (s > mx) mx = s;
  }
  if (mn == 0 && mx == 0)  hwSet(HW_MIC, HW_FAIL, "all zeros - not connected? (SD=GPIO%d)", I2S_MIC_SD);
  else if (mn == mx)       hwSet(HW_MIC, HW_FAIL, "stuck at %ld - check wiring / SEL pin", (long)mn);
  else                     hwSet(HW_MIC, HW_OK,   "signal ok (range %ld..%ld)", (long)mn, (long)mx);
}

// Brings up every peripheral, one at a time, AFTER the web server is already
// answering. Each step records its result in hw[] for GET /hw; `hwStep` shows
// which step is running, so if a part ever hangs the UI names it.
void hwInitTask(void*) {
  hwStep = "flash";
  loadTrims();
  motionLoadSettings();
  dlogPrintf("setup: trims %d %d %d %d", trim[0],trim[1],trim[2],trim[3]);
#if BOB_VISUAL_BRINGUP
  hwSet(HW_FLASH, HW_OK, "NVS trims loaded; clip storage disabled");
#else
  // Sound-clip storage. Auto-format on first mount failure.
  fsReady = LittleFS.begin(true);
  if (!fsReady) {
    dlogPrintf("LittleFS mount failed, attempting explicit format...");
    LittleFS.format();
    fsReady = LittleFS.begin(true);
  }
  if (fsReady) {
    unsigned freeKB = (unsigned)((LittleFS.totalBytes()-LittleFS.usedBytes())/1024);
    dlogPrintf("setup: LittleFS ok — %u KB total, %u KB free (max %d s recording)",
               (unsigned)(LittleFS.totalBytes()/1024), freeKB, maxRecordSecs());
    hwSet(HW_FLASH, HW_OK, "%u KB free", freeKB);
  } else {
    dlogPrintf("setup: LittleFS MOUNT FAILED — pick a partition scheme with SPIFFS in Tools menu");
    hwSet(HW_FLASH, HW_FAIL, "mount failed - choose a partition scheme with SPIFFS");
  }

#endif

  // Attach 4 servos directly to GPIO pins (no PCA9685 / I²C needed), one LEDC
  // channel each at 50 Hz.
  hwStep = "servos";
  static const char* const SERVO_NAMES[4] = {"LH","LF","RH","RF"};
  int attached = 0;
  for (int i = 0; i < 4; i++) {
    bool ok = servos[i].attach(servoPins[i], i);
    // Print immediately so a hang later doesn't swallow the result.
    dlogPrintf("servo[%d] %s GPIO%d LEDC ch%d: %s",
               i, SERVO_NAMES[i], servoPins[i], i, ok ? "OK" : "FAIL - check GPIO");
    if (ok) {
      attached++;
      if (motion.config.customNeutral) {
        setServo(i,(int)lroundf(motion.config.neutral[i]));
      } else if (BOB_VISUAL_BRINGUP) {
        servos[i].writeMicroseconds(1500); // raw centre before fitting new horns
        servoCur[i] = 90 - trim[i]; // logical position consistent with raw centre
      } else {
        setServo(i, 90);
      }
      delay(250); // stagger initial motion to reduce simultaneous startup load
    }
  }
  dlogPrintf("setup: %d/4 servos attached — neutral pulse sent (MIN_US=%d MAX_US=%d)",
             attached, SERVO_MIN_US, SERVO_MAX_US);
  if (attached == 4) hwSet(HW_SERVOS, HW_NOCHECK, "PWM on GPIO %d/%d/%d/%d (no feedback)",
                           servoPins[0], servoPins[1], servoPins[2], servoPins[3]);
  else               hwSet(HW_SERVOS, HW_FAIL, "only %d/4 PWM channels attached — check GPIO wiring", attached);

#if BOB_VISUAL_BRINGUP
  if (BOB_SPEAKER_TEST) {
    hwStep = "speaker";
    i2s_speaker_init();
  } else {
    hwSet(HW_SPEAKER, HW_NOCHECK, "disabled in bring-up profile");
  }
  hwStep = "camera";
  cameraInit();
  hwStep = "microphone";
  if (BOB_MIC_TEST) {
    loadMicCfg();
#if !ARDUINO_USB_CDC_ON_BOOT
    // GPIO43/44 are the mic clocks and UART0 TX/RX. If IDE USB CDC is off,
    // release UART0 before I2S claims those pads; the RTC /logs endpoint
    // remains available for diagnostics after this point.
    Serial.flush();
    Serial.end();
    serialLogEnabled = false;
    dlogPrintf("setup: USB CDC disabled; UART0 released for mic GPIO43/44; use /logs");
#endif
    i2s_mic_init();
    if (micInstalled) checkMic();
  } else {
    hwSet(HW_MIC, HW_NOCHECK, "disabled in visual bring-up");
  }
  hwStep = "imu";
  if (BOB_IMU_TEST) {
    imuReady = imuInit(true);
    loadImuCal();
    if (imuReady) {
      hwSet(HW_IMU, HW_OK, "%s @ 0x%02X; WHO_AM_I=0x%02X", imuChipName(), imuAddr, imuWho);
    } else {
      hwSet(HW_IMU, HW_FAIL, "no MPU-6500/9250 @ 0x68/0x69; SDA%d SCL%d", IMU_SDA, IMU_SCL);
    }
  } else {
    hwSet(HW_IMU, HW_NOCHECK, "disabled in visual profile");
  }
  hwStep = "servos + screen + camera + audio + imu ready";
  dlogPrintf("setup: VISUAL ready; servos=%d/4 screen=initialized camera=%s mic=%s speaker=%s imu=%s",
             attached, camReady ? "OK" : "FAIL", micInstalled ? "OK" : "FAIL",
             BOB_SPEAKER_TEST ? "enabled" : "disabled", imuReady ? imuChipName() : "offline");
  hwInitDone = true; // screen/servos remain usable even when camera is missing
  vTaskDelete(nullptr);
  return;
#endif

  hwStep = "screen";
  screenSetup();
  dlogPrintf("setup: screen ready");
  hwSet(HW_SCREEN, HW_NOCHECK, "ST7735 driven (write-only, can't detect)");
  showNetScreen();   // shows the IP on the TFT for 5 s

  hwStep = "audio";
  i2s_speaker_init();
  loadMicCfg();
  i2s_mic_init();
  checkMic();

  hwStep = "camera";
  cameraInit();

  hwStep = "imu";
  imuReady = imuInit(true);
  loadImuCal();
  if (imuReady) {
    dlogPrintf("setup: %s IMU ready", imuChipName());
    hwSet(HW_IMU, HW_OK, "%s @ 0x%02X", imuChipName(), imuAddr);
  } else {
    hwSet(HW_IMU, HW_FAIL, "no MPU-6500/9250 @ 0x68/0x69; SDA%d SCL%d", IMU_SDA, IMU_SCL);
  }

  hwStep = "done";
  hwInitDone = true;
  dlogPrintf("setup: hardware init done (heap=%u)", ESP.getFreeHeap());
  vTaskDelete(nullptr);
}

// Camera pins are separate from the legacy GPIO33-37 assignments. In recovery
// mode only initialize the camera; keep hwInitDone false so normal handlers
// and loop animations cannot touch the uninitialized display/audio/servos.
void cameraRecoveryTask(void*) {
  hwStep = "recovery: camera starting";
  dlogPrintf("setup: camera-only recovery; other hardware blocked by pin map");
  cameraInit();
  hwStep = camReady ? "recovery: camera ready" : "recovery: camera failed";
  dlogPrintf("setup: recovery camera=%s; rewire GPIO33-37 for full operation",
             camReady ? "OK" : "FAIL");
  vTaskDelete(nullptr);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n========================================");
  Serial.println("  BOB V2 ESP32-S3 BOOTING...");
  Serial.println("========================================");
  Serial.flush();

  // FIRST: the log ring lives in RTC memory that holds random garbage after a
  // power-on, so nothing may log before dlogBoot() has validated it.
  dlogBoot();
  randomSeed(esp_random());   // use hardware RNG — avoids ADC driver conflict

  if (psramFound()) {
    dlogPrintf("setup: PSRAM %u KB", (unsigned)(ESP.getPsramSize() / 1024));
    hwSet(HW_PSRAM, HW_OK, "%u KB", (unsigned)(ESP.getPsramSize() / 1024));
  } else {
    dlogPrintf("setup: no PSRAM (Tools > PSRAM is Disabled)");
    hwSet(HW_PSRAM, HW_NOCHECK, "disabled in Tools > PSRAM (camera uses internal RAM)");
  }

#if BOB_VISUAL_BRINGUP
  // Visible feedback before any WiFi join or camera probe. No browser needed.
  hwStep = "screen";
  screenSetup();
  hwSet(HW_SCREEN, HW_NOCHECK, "ST7735 initialized; MOSI1 CLK2 CS21 DC42 RST47");
  dlogPrintf("setup: screen initialized before WiFi (MOSI=1 CLK=2 CS=21 DC=42 RST=47)");
#endif

  loadWifiCreds();   // must be before wifiConnect()
  wifiConnect();

  // The visual profile has already initialized its write-only TFT. Start
  // the UI before camera/servo initialization so diagnostics stay available.
  registerRoutes();
  server.begin();
  mttAsyncSetup();   // start async TTS server on port 8080
  // OTA must be started after WiFi and after registerRoutes/server.begin.
  // collectHeaders() (called inside registerRoutes) must precede ArduinoOTA.begin().
  otaSetup();
  startCamServer();
#if BOB_VISUAL_BRINGUP
  showNetScreen();
#endif
  dlogPrintf("setup: web server up - http://%s  home=%s  OTA port 3232  (heap=%u)",
             ottoIP.c_str(), homeIP.length() ? homeIP.c_str() : "-", ESP.getFreeHeap());

  // Do not hand an active octal RAM bus to LEDC/SPI/I2S. This check does not
  // certify that reserved memory pins are usable when PSRAM is disabled.
  const int peripheralPins[] = {SERVO_PIN_LH, SERVO_PIN_LF, SERVO_PIN_RH,
    SERVO_PIN_RF, TFT_MOSI, TFT_CLK, TFT_CS, TFT_DC, TFT_RST
#if BOB_SPEAKER_TEST
    , I2S_SPK_BCLK, I2S_SPK_LRC, I2S_SPK_DIN
#endif
#if !BOB_VISUAL_BRINGUP
    , I2S_MIC_SCK, I2S_MIC_WS, I2S_MIC_SD
#elif BOB_MIC_TEST
    , I2S_MIC_SCK, I2S_MIC_WS, I2S_MIC_SD
#endif
#if BOB_IMU_TEST
    , IMU_SDA, IMU_SCL
#endif
  };
  bool memoryConflict = false;
  for (int pin : peripheralPins) {
    if (psramFound() && pin >= 33 && pin <= 37) {
      memoryConflict = true;
      dlogPrintf("PIN CONFLICT: GPIO%d assigned to peripheral and octal PSRAM", pin);
    }
  }
  if (memoryConflict) {
    hwInitError = "Camera-only recovery: other hardware blocked by PSRAM pin conflict. See BOB_V2/WIRING.md.";
    hwStep = "recovery: camera queued";
    for (int i = HW_FLASH; i < HW_COUNT; i++)
      hwSet((HwId)i, HW_FAIL, "startup blocked: PSRAM pin conflict; see /logs");
    dlogPrintf("setup: %s", hwInitError);
    hwSet(HW_CAMERA, HW_PENDING, "starting independently in recovery mode");
    if (xTaskCreatePinnedToCore(cameraRecoveryTask, "camrecovery", 16384,
                              nullptr, 1, nullptr, 1) != pdPASS) {
      hwStep = "recovery: camera task failed";
      hwSet(HW_CAMERA, HW_FAIL, "camera task allocation failed");
      dlogPrintf("setup: camera recovery task allocation failed heap=%u", ESP.getFreeHeap());
    }
    return;
  }

  // Peripherals come up in the background; loop() keeps serving the UI.
  if (xTaskCreatePinnedToCore(hwInitTask, "hwinit", 16384, nullptr, 1, nullptr, 1) != pdPASS) {
    hwInitError = "Hardware init task allocation failed; insufficient internal RAM.";
    hwStep = "failed: init task allocation";
    for (int i = HW_FLASH; i < HW_COUNT; i++)
      hwSet((HwId)i, HW_FAIL, "init task allocation failed");
    dlogPrintf("setup: %s heap=%u", hwInitError, ESP.getFreeHeap());
  }
}

void loop() {
  unsigned long loopStart = millis();   // for the stall log at the bottom
  // ArduinoOTA.handle() must be called every loop iteration while WiFi is up
  // so the espota service can accept an incoming connection. It is non-blocking
  // (returns immediately when no OTA is in progress). Placed before
  // server.handleClient() so a pending OTA isn't delayed by a slow HTTP request.
  ArduinoOTA.handle();
  if (hwInitDone) motionTick();  // expire the lease before accepting another command
  server.handleClient();   // the ONLY place handleClient() may be called
  // During background initialization, only the visual profile may animate
  // its already initialized display; other hardware waits for hwInitDone.
#if BOB_VISUAL_BRINGUP
  // Only loop/setup touch the TFT in this profile; the worker handles camera
  // and servos. Animate even while camera initialization is pending.
  wifiStatusTick();
  screenTick();
  if (!hwInitDone) { delay(2); return; }
#else
  if (!hwInitDone) { delay(2); return; }
  screenTick();
#endif

  // ── IMU: read + lift detection + scared reaction ────────────
  //
  // The IMU is optional hardware. Everything in this block is written so that a
  // chip which is missing, unplugged mid-run, or wired through a connector that
  // rattles loose while the robot dances degrades to "no lift detection" and
  // nothing worse. The web UI, the face and the servos never wait on it.
  if (!BOB_VISUAL_BRINGUP || BOB_IMU_TEST) {
    unsigned long now = millis();

    // ── Reaction routine ──────────────────────────────────────
    // Runs first and unconditionally: once a lift has fired, the routine owns
    // the feet and the face until it finishes, whatever the IMU does in the
    // meantime (including falling off the bus entirely).
    reactTickFeet();

    // Legs have stopped and the chassis has stopped ringing → start sampling a
    // fresh baseline.
    if (reactState == RS_SETTLE && now - reactStart >= REACT_SETTLE_MS) {
      reactState  = RS_RECAL;
      reactStart  = now;
      recalCount  = 0;
      for (int i = 0; i < 6; i++) recalSum[i] = 0;
    }
    // The IMU went quiet mid-recalibration. Don't sit in this phase forever —
    // give the face and lift detection back, uncalibrated.
    else if (reactState == RS_RECAL && now - reactStart >= REACT_RECAL_TIMEOUT) {
      imuCalValid = false;
      endLiftReaction("recal timed out — IMU not answering");
    }

    // ── Offline: slow reconnect probe ─────────────────────────
    // Poll the bus at IMU_RETRY_MS, not IMU_PERIOD_MS. imuInit() now does a
    // fast bare WHO_AM_I check first (<1 ms) so a missing chip costs almost
    // nothing per probe — but we still keep 2 s spacing to avoid unnecessary
    // I2C chatter and to give a recently hot-plugged chip time to boot.
    //
    // ── I2C safety: never touch the bus while the feet are slewing ──────────
    // The scared reaction is the ONLY motion that runs concurrently with IMU
    // polling — walk/dance are called from handleMove() and block loop() for
    // their whole duration, so the IMU is never read mid-move there. During
    // RS_FLIP loop() keeps spinning and would otherwise read the IMU every
    // 50 ms *while the foot servos slew*. That slewing couples heavy noise onto
    // the weakly-pulled-up I2C bus (see imuInit's note), and a transaction
    // corrupted badly enough can wedge the ESP32 I2C peripheral so Wire never
    // returns — hanging loop() and freezing the panel mid-repaint. That is the
    // "scared → screen goes white and never comes back until restart" bug, and
    // it is exactly why dance (which never reads the IMU mid-move) is immune.
    // Nothing needs the IMU during the flips: the reaction is clock-driven,
    // lift detection is off, and recalibration only samples once the feet have
    // parked (RS_RECAL runs with the legs still). So just skip polling then.
    bool imuPollOk = (reactState != RS_FLIP);
    if (imuPollOk && !imuReady) {
      if (now >= nextImuRead) {
        nextImuRead = now + IMU_RETRY_MS;
        if (imuInit(false)) {
          imuReady      = true;
          imuFailStreak = 0;
          imuLastOkMs   = now;
          imuReconnects++;
          // A chip that just reappeared may be a different one, or the same one
          // remounted at a new angle. Either way the old baseline is a guess —
          // require a fresh calibration rather than acting on a stale zero.
          imuCalValid = false;
          dlogPrintf("imu: reconnected (#%lu) — recalibrate to re-arm lift detection",
                     (unsigned long)imuReconnects);
        }
      }
    }
    else if (imuPollOk && now >= nextImuRead) {
      nextImuRead = now + IMU_PERIOD_MS;
      if (!imuReadRaw()) {
        // ── Read failed ─────────────────────────────────────────
        // One bad read is nothing — servo switching noise couples into the bus
        // and corrupts a transfer now and then. A run of them means the chip is
        // actually gone, so stop hammering it.
        if (++imuFailStreak >= IMU_FAIL_LIMIT) {
          imuReady    = false;
          imuCalValid = false;      // no sensor => no trustworthy baseline
          nextImuRead = now + IMU_RETRY_MS;
          dlogPrintf("imu: lost after %d failed reads (up %lu ms) — probing every %d ms; "
                     "UI and servos unaffected",
                     imuFailStreak,
                     (unsigned long)(imuLastOkMs ? now - imuLastOkMs : 0),
                     IMU_RETRY_MS);
          // The reaction routine is NOT cancelled here on purpose. It is driven
          // by the clock, not the sensor, so it finishes its flips and parks the
          // feet at neutral even with the chip gone. Only the recalibration
          // phase needs the IMU, and that has its own timeout above.
          imuLifted = false;
        }
      } else {
        imuFailStreak = 0;
        imuLastOkMs   = now;

        // Diagnostic peak-hold of RAW gyro magnitude (runs every read, ungated).
        {
          long gm=(long)sqrtf((float)((long)imu_gx*imu_gx
                                     +(long)imu_gy*imu_gy
                                     +(long)imu_gz*imu_gz));
          if (gm > imuGmagPeak) imuGmagPeak = gm;
        }

        // ── Threshold hit counter ─────────────────────────────────
        // Counts every time the accel delta crosses imuLiftDelta (2 consecutive
        // reads). Works from RS_IDLE only — the reaction itself is not retriggered
        // while the legs are already moving.
        // 4 hits within LIFT_WINDOW_MS → startLiftReaction().
        // Any gap longer than LIFT_WINDOW_MS resets the count.
        // NOTE: customNeutral no longer gates this — shake/lift detection must
        // work even when a custom neutral pose is set (that's the normal state
        // on an assembled robot). The reaction's foot-flips are relative to the
        // current neutral, so they're fine with a custom pose.
        if (!motion.armed && reactState == RS_IDLE && !scaredMode && imuCalValid && liftDetectEnabled) {
          static uint8_t rawCount = 0;
          long mag   = accelMag(imu_ax, imu_ay, imu_az);
          long delta = labs(mag - imuRestMag);
          // Also watch rotation: a SHAKE barely changes accel magnitude but
          // spins the gyro hard. Trigger on either accel lift OR gyro shake.
          // RAW gyro magnitude (not bias-subtracted): the post-reaction recal can
          // fold shaking motion into the gyro bias and kill repeat detection.
          // Raw gyro is ~tiny at rest and huge during any shake, so it's robust.
          long gmag = (long)sqrtf((float)((long)imu_gx*imu_gx
                                        + (long)imu_gy*imu_gy
                                        + (long)imu_gz*imu_gz));
          if (delta > imuLiftDelta || gmag > imuShakeGyro) rawCount++;
          else                                             rawCount = 0;

          if (rawCount >= 2) {
            rawCount = 0;
            // Start or check the 2-second window
            if (liftHitCount == 0) {
              liftWindowStart = now;   // first hit — open the window
            } else if (now - liftWindowStart > LIFT_WINDOW_MS) {
              // Window expired — restart from this hit
              liftHitCount    = 0;
              liftWindowStart = now;
              dlogPrintf("imu: window expired, restarting count");
            }
            liftHitCount++;
            dlogPrintf("imu: hit %d/%d (%lums)",
                       (int)liftHitCount, LIFT_HIT_NEEDED,
                       now - liftWindowStart);
            if (liftHitCount >= LIFT_HIT_NEEDED) {
              liftHitCount    = 0;
              liftWindowStart = 0;
              startLiftReaction();
            }
          }
        }

        // ── Recalibrating ───────────────────────────────────────
        // Adopt whatever pose the robot ended up in. No NVS write: this baseline
        // is transient by design (the next lift replaces it), and writing flash
        // on every pick-up would burn the sector for no benefit. Only an explicit
        // /imucal persists.
        if (reactState == RS_RECAL) {
          recalSum[0] += imu_ax; recalSum[1] += imu_ay; recalSum[2] += imu_az;
          recalSum[3] += imu_gx; recalSum[4] += imu_gy; recalSum[5] += imu_gz;
          if (++recalCount >= REACT_RECAL_SAMPLES) {
            imuCalAx = (int16_t)(recalSum[0] / recalCount);
            imuCalAy = (int16_t)(recalSum[1] / recalCount);
            imuCalAz = (int16_t)(recalSum[2] / recalCount);
            imuCalGx = (int16_t)(recalSum[3] / recalCount);
            imuCalGy = (int16_t)(recalSum[4] / recalCount);
            imuCalGz = (int16_t)(recalSum[5] / recalCount);
            imuRestMag  = accelMag(imuCalAx, imuCalAy, imuCalAz);
            accelPitchRoll(imuCalAx, imuCalAy, imuCalAz, &imuCalPitch, &imuCalRoll);
            imuCalValid = true;
            imuLifted   = false;
            dlogPrintf("react: recal a=%d,%d,%d restMag=%ld (%d samples)",
                       (int)imuCalAx, (int)imuCalAy, (int)imuCalAz,
                       imuRestMag, recalCount);
            endLiftReaction("recalibrated to new resting pose");
          }
        }
        // ── Lift detection (RS_IDLE only) ───────────────────────
        // Compare the magnitude of the current acceleration vector against the
        // calibrated rest magnitude. When the robot is lifted, tilted or shaken
        // the net vector deviates from 1 g by more than imuLiftDelta — which
        // works at ANY resting angle, because the baseline is whatever pose it
        // was last calibrated in.
        //
        // Only the RISING edge exists now. There is no "set down" event to
        // detect: the reaction routine finishes by adopting the robot's new
        // resting pose as the baseline, so by definition it is at rest again
        // when control returns here, and endLiftReaction() has already cleared
        // imuLifted. That removed the whole falling-edge/hysteresis path.
        //
        // The 2-sample requirement stays: it is what stops a single noisy
        // reading — servo switching noise coupling into the I2C bus, say — from
        // firing the routine spuriously.
      }
    }
  }

  motionTick(); // consumes the newest MPU sample; playback never blocks HTTP

  // Heartbeat every 15 s — after a crash the last heartbeat in /logs shows
  // how long it had been running and whether the heap was shrinking.
  static unsigned long nextBeat = 0;
  if (millis() >= nextBeat) {
    dlogPrintf("heartbeat: heap=%u min=%u wifi=%s ip=%s",
               ESP.getFreeHeap(), ESP.getMinFreeHeap(),
               apMode ? "AP" : (WiFi.status() == WL_CONNECTED ? "OK" : "DOWN"),
               ottoIP.c_str());
    nextBeat = millis() + 15000;
  }

  // ── Loop-stall log ────────────────────────────────────────────
  // A healthy loop() pass is a few ms. A long pass means something blocked. Log
  // any unusual stall (rate-limited to 1/s) with the current mode, so the
  // Device Log shows what the robot was doing if/when the screen froze. Blocking
  // web actions (dance, record, playback) legitimately trip this once; a stall
  // during the otherwise non-blocking scared reaction is the fingerprint of the
  // I2C-during-flip hang.
  {
    unsigned long dt = millis() - loopStart;
    static unsigned long lastStallLog = 0;
    if (dt >= 300 && millis() - lastStallLog > 1000) {
      lastStallLog = millis();
      dlogPrintf("STALL loop %lums react=%d scared=%d dance=%d listen=%d expr=%d heap=%u",
                 dt, (int)reactState, scaredMode ? 1 : 0, danceMode ? 1 : 0,
                 listenActive() ? 1 : 0, expression, ESP.getFreeHeap());
    }
  }
}
