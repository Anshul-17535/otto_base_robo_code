#pragma once

// Validate all enabled signal pins for the N16R8 visual profile.
namespace bob_visual_pins {
constexpr int visual[] = {
  SERVO_PIN_LH, SERVO_PIN_LF, SERVO_PIN_RH, SERVO_PIN_RF,
  TFT_MOSI, TFT_CLK, TFT_CS, TFT_DC, TFT_RST,
  XCLK_GPIO_NUM, SIOD_GPIO_NUM, SIOC_GPIO_NUM,
  Y2_GPIO_NUM, Y3_GPIO_NUM, Y4_GPIO_NUM, Y5_GPIO_NUM,
  Y6_GPIO_NUM, Y7_GPIO_NUM, Y8_GPIO_NUM, Y9_GPIO_NUM,
  VSYNC_GPIO_NUM, HREF_GPIO_NUM, PCLK_GPIO_NUM
};
constexpr int mic[] = {I2S_MIC_SCK, I2S_MIC_WS, I2S_MIC_SD};
constexpr int speaker[] = {I2S_SPK_BCLK, I2S_SPK_LRC, I2S_SPK_DIN};
constexpr int imu[] = {IMU_SDA, IMU_SCL};
constexpr bool inVisual(int p) {
  for (unsigned i = 0; i < sizeof(visual) / sizeof(visual[0]); ++i)
    if (p == visual[i]) return true;
  return false;
}
constexpr bool validVisual() {
  for (unsigned i = 0; i < sizeof(visual) / sizeof(visual[0]); ++i) {
    const int p = visual[i];
    // Exclude memory, native USB, UART0 and boot-strapping pins.
    if (!((p >= 1 && p <= 18 && p != 3) || p == 21 ||
          (p >= 38 && p <= 42) || p == 47 || p == 48)) return false;
    for (unsigned j = 0; j < i; ++j)
      if (p == visual[j]) return false;
  }
  return true;
}
constexpr bool validMic() {
  for (unsigned i = 0; i < sizeof(mic) / sizeof(mic[0]); ++i) {
    const int p = mic[i];
    if (!(p == 43 || p == 44 || p == 48)) return false;
    if (inVisual(p)) return false;
    for (unsigned j = 0; j < i; ++j)
      if (p == mic[j]) return false;
  }
  return true;
}
constexpr bool validSpeaker() {
  for (unsigned i = 0; i < sizeof(speaker) / sizeof(speaker[0]); ++i) {
    const int p = speaker[i];
    if (p < 0 || p > 48 || (p >= 26 && p <= 37) || p == 19 || p == 20)
      return false;
    if (inVisual(p)) return false;
    for (unsigned j = 0; j < sizeof(mic) / sizeof(mic[0]); ++j)
      if (p == mic[j]) return false;
    for (unsigned j = 0; j < i; ++j)
      if (p == speaker[j]) return false;
  }
  return true;
}
constexpr bool validImu() {
  for (unsigned i = 0; i < sizeof(imu) / sizeof(imu[0]); ++i) {
    const int p = imu[i];
    // Only GPIO0/46 remain with the existing peripherals. GPIO0 must be high
    // for normal boot; disconnect MPU signals for ROM download (46 must be low).
    if (!(p == 0 || p == 46)) return false;
    if (inVisual(p)) return false;
    for (unsigned j = 0; j < sizeof(mic) / sizeof(mic[0]); ++j)
      if (BOB_MIC_TEST && p == mic[j]) return false;
    for (unsigned j = 0; j < sizeof(speaker) / sizeof(speaker[0]); ++j)
      if (BOB_SPEAKER_TEST && p == speaker[j]) return false;
    for (unsigned j = 0; j < i; ++j)
      if (p == imu[j]) return false;
  }
  return IMU_SDA == 46 && IMU_SCL == 0;
}
static_assert(validVisual(), "Visual pin map overlaps or uses a reserved pin; check WIRING.md");
static_assert(!BOB_MIC_TEST || validMic(),
              "Mic pin map overlaps an active peripheral; check WIRING.md");
static_assert(!BOB_SPEAKER_TEST || validSpeaker(),
              "Speaker pin map overlaps an active peripheral; check WIRING.md");
static_assert(!BOB_IMU_TEST || validImu(),
              "IMU pin map overlaps an active peripheral; check WIRING.md");
}
