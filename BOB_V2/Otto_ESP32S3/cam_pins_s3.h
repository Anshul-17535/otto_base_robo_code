// cam_pins_s3.h — OV2640 CSI camera pin map for ESP32-S3-N16R8
//
// These pins are FIXED by the module's CSI connector wiring.
// Do NOT reassign them to any other peripheral.
//
// GPIOs used by the camera: 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18
// All remaining peripherals (TFT, I2S, servos, I2C) use GPIOs outside this range.
//
// Board: ESP32S3 Dev Module  |  Flash: 16MB  |  PSRAM: OPI PSRAM (8MB)
#pragma once

// Power-down and hardware-reset lines are not wired on this module.
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1

// Master clock output to the OV2640
#define XCLK_GPIO_NUM    15

// SCCB (I2C-like) camera control bus — separate from Wire / MPU IMU
#define SIOD_GPIO_NUM     4   // data
#define SIOC_GPIO_NUM     5   // clock

// Parallel DVP data bus  (D0–D7 in OV2640 datasheet = Y2–Y9 in esp_camera)
#define Y2_GPIO_NUM      11   // D0
#define Y3_GPIO_NUM       9   // D1
#define Y4_GPIO_NUM       8   // D2
#define Y5_GPIO_NUM      10   // D3
#define Y6_GPIO_NUM      12   // D4
#define Y7_GPIO_NUM      18   // D5
#define Y8_GPIO_NUM      17   // D6
#define Y9_GPIO_NUM      16   // D7

// Sync signals
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM    13

// XCLK frequency in Hz — 20 MHz works reliably with OV2640
#define CAM_XCLK_HZ  20000000
