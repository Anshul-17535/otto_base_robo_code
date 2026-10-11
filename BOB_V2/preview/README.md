# Dashboard preview and validation

`index.html` is extracted from the firmware's `HTML_PAGE` after the styling update. It is a visual preview, not a robot simulator. API requests need a real robot; when opened locally, unavailable hardware status is expected. The screenshots were captured with API requests returning HTTP 503, not connected to physical hardware.

Verified on 2026-09-26:
- Arduino ESP32 core 3.3.11 compilation succeeded, S3 / 16 MB / huge_app / PSRAM disabled. Firmware: 1,321,320 bytes; static RAM: 67,272 bytes.
- Chrome at 1440, 390 and 320 px: no horizontal page overflow or uncaught JavaScript errors.
- All 17 existing panels, 74 buttons, original IDs and original control scripts preserved.
- No firmware upload or physical servo, camera, audio, or reset test performed.

See ../WIRING.md for the unresolved board-specific pin conflicts and the SG92R bring-up procedure. Do not treat the successful build as verification of the existing wiring.
