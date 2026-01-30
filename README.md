# ESP-Mesh-Lite Test Project

Example project demonstrating the ESP-Mesh-Lite Arduino library.

## Requirements

- PlatformIO with [pioarduino](https://github.com/pioarduino/platform-espressif32) platform (ESP-IDF 5.x)
- ESP32, ESP32-S2, ESP32-S3, ESP32-C2, ESP32-C3, or ESP32-C6

## Setup

1. Copy `.env.example` to `.env`
2. Edit `.env` with your credentials:
   ```
   WIFI_SSID="YourWiFiSSID"
   WIFI_PASSWORD="YourWiFiPassword"
   MESH_SOFTAP_SSID="ESP"
   MESH_SOFTAP_PASSWORD="12345678"
   MESH_ID=77
   ```

## Build & Upload

```bash
# Build
pio run

# Upload
pio run -t upload

# Monitor
pio device monitor
```

## Supported Boards

| Environment | Board |
|-------------|-------|
| esp32 | ESP32 DevKit |
| esp32s3 | ESP32-S3 DevKitC-1 |
| esp32c3 | ESP32-C3 DevKitM-1 |
| lilygo-t3-s3 | LilyGo T3-S3 |

## License

Apache-2.0
