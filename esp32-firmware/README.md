# Codex Companion ESP32 Firmware

Phase 2 firmware target for the Waveshare ESP32-S3-Touch-AMOLED-2.16.

This is an ESP-IDF project because Waveshare's official examples provide a BSP component for the board, including the CO5300 AMOLED display, CST9217 touch controller, and LVGL integration.

## Requirements

- ESP-IDF 5.5 or newer in PATH
- Waveshare ESP32-S3-Touch-AMOLED-2.16 connected by USB
- The companion service running on the same LAN

## Configure

```powershell
cd esp32-firmware
idf.py set-target esp32s3
idf.py menuconfig
```

Set:

- `Codex Companion > WiFi SSID`
- `Codex Companion > WiFi password`
- `Codex Companion > WebSocket auth token`

The generated `sdkconfig` is ignored by git because it contains local WiFi credentials and the companion token.

## Build and flash

```powershell
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with the serial port shown by Windows Device Manager.

## Runtime

The firmware:

- Starts the Waveshare BSP display and LVGL touch input.
- Connects to WiFi.
- Queries mDNS for `_codex-companion._tcp.local`.
- Opens `ws://<discovered-host>:<discovered-port>`.
- Authenticates with the companion token.
- Renders session state, latest event, and permission requests.
- Sends `allow` / `deny` actions when the touchscreen buttons are pressed.
