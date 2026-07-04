# Waveshare ESP32-S3-Touch-AMOLED-2.16 Notes

Sources:

- https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16/
- https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16

Relevant hardware facts from Waveshare:

- MCU: ESP32-S3R8 with 8MB PSRAM and 16MB flash.
- Display: 2.16 inch AMOLED, 480x480, CO5300 over QSPI.
- Touch: CST9217 over shared I2C.
- Shared I2C: `GPIO14=SCL`, `GPIO15=SDA`.
- Display QSPI: data `GPIO4/5/6/7`, clock `GPIO38`, chip select `GPIO12`.
- Display reset: `GPIO39`.
- Touch interrupt/reset: `GPIO11` / `GPIO40`.

Implementation decision:

- Use ESP-IDF rather than a pure Arduino sketch for the first hardware firmware because Waveshare publishes an ESP-IDF BSP component that initializes CO5300, CST9217, and LVGL together.
- Keep WiFi credentials and the companion auth token in `sdkconfig`, which is ignored by git.
- Require mDNS discovery of `_codex-companion._tcp` by default, with an optional fallback host for lab debugging.
