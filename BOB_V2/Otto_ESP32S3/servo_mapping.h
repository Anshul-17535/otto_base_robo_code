#pragma once
#include <stdint.h>

namespace bobservo {
constexpr uint32_t MAGIC = 0x534D5031; // SMP1, versioned NVS layout
struct Layout { uint32_t magic; int32_t pins[4]; int32_t trims[4]; };

// Safe servo GPIOs on ESP32-S3-N16R8 with the current peripheral map:
//   4-18  camera   19-20 USB   21 TFT-CS   26-37 flash/PSRAM
//   38-41 servos   42 TFT-DC   43-44 mic clk   45 amp-LRC
//   46 IMU-SDA   47 TFT-RST   48 mic-DOUT
//   0 IMU-SCL   1-2 TFT MOSI/CLK   3 amp-BCLK   14 amp-DIN
// Only 38,39,40,41 remain unassigned and safe for servo PWM.
// Extend this list here if different GPIOs are ever freed up.
constexpr int ALLOWED_PINS[] = {38, 39, 40, 41};
constexpr int ALLOWED_COUNT  = 4;

inline bool isAllowed(int pin) {
  for (int i = 0; i < ALLOWED_COUNT; i++) if (ALLOWED_PINS[i] == pin) return true;
  return false;
}
inline bool validPins(const int* pins) {
  for (int i=0;i<4;i++) {
    if (!isAllowed(pins[i])) return false;
    for (int j=0;j<i;j++) if (pins[j]==pins[i]) return false;
  }
  return true;
}
inline Layout makeLayout(const int* pins, const int* trims) {
  Layout result{}; result.magic=MAGIC;
  for (int i=0;i<4;i++) { result.pins[i]=pins[i]; result.trims[i]=trims[i]; }
  return result;
}
inline bool valid(const Layout& layout) {
  int pins[4];
  if (layout.magic!=MAGIC) return false;
  for (int i=0;i<4;i++) {
    pins[i]=layout.pins[i];
    if (layout.trims[i]<-45 || layout.trims[i]>45) return false;
  }
  return validPins(pins);
}
// Index each new joint by its old physical GPIO. No PWM writes are needed.
inline bool permutation(const int* previous, const int* next, int* indices) {
  if (!validPins(previous) || !validPins(next)) return false;
  for (int i=0;i<4;i++)
    for (int j=0;j<4;j++) if (next[i]==previous[j]) indices[i]=j;
  return true;
}
}
