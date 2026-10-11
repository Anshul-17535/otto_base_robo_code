# Pinned async TTS dependencies

`ota_flash.sh` explicitly selects these project-local libraries over older
Arduino sketchbook installations. Source and upstream licenses are included.

- [ESPAsyncWebServer v3.12.1](https://github.com/ESP32Async/ESPAsyncWebServer/releases/tag/v3.12.1)
- [AsyncTCP v3.5.0](https://github.com/ESP32Async/AsyncTCP/releases/tag/v3.5.0)

These replace ESPAsyncWebServer 3.1.0 / AsyncTCP 1.1.4 for the ESP32 Arduino
3.3.11 build. The old server fails on removed mbedTLS `_ret` APIs.
`Otto_ESP32S3/build_opt.h` pins the async task to Core 0 with its watchdog enabled.
For manual CLI builds, pass both directories with `--library` as in the OTA
script. No system-wide library installation is required.
