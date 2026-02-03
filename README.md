# ESP-Mesh-Lite + MQTT Gateway

[![Version](https://img.shields.io/badge/version-1.1.0-blue.svg)](CHANGELOG.md)
[![License](https://img.shields.io/badge/license-Apache--2.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32-orange.svg)](https://www.espressif.com)

ESP32 mesh network with MQTT gateway using [ESP-Mesh-Lite](https://github.com/espressif/esp-mesh-lite). Supports multi-root topology where multiple nodes can connect directly to the router.

## Features

- **Multi-root topology** - All nodes try router first, fall back to mesh when unavailable
- **Binary protocol** - Optimized 16-byte status messages (vs ~60 bytes JSON)
- **Unified MQTT topics** - Consistent device-centric topic structure
- **Auto mesh fallback** - Nodes automatically rejoin mesh when router connection lost
- **OLED display** - Configurable status display (SSD1306/SH1106, 128x64/128x32)
- **Watchdog timer** - Auto-restart on hang (configurable timeout)
- **OTA updates** - MQTT-triggered OTA with progress/status reporting

## Architecture

```
Multi-Root Mode (router available to all):
    ┌──────────┐
    │  Router  │
    └────┬─────┘
    ┌────┼─────┐
    ▼    ▼     ▼
  [A]  [B]   [C]     ← All are root (level 1), each publishes to MQTT

Mesh Fallback (C loses router):
    ┌──────────┐
    │  Router  │
    └────┬─────┘
         │
    ┌────┴────┐
    ▼         ▼
  [A]       [B]      ← Root nodes (level 1)
             │
             ▼
           [C]       ← Falls back to mesh (level 2), status forwarded via B
```

## Project Structure

```
src/
├── main.cpp              # Application entry point
├── config.h              # Configuration (mesh, MQTT, display, watchdog)
├── ota/
│   ├── ota_handler.h/cpp     # HTTPS OTA handler (URL-based)
├── mesh/
│   ├── mesh_handler.h/cpp    # Mesh communication wrapper
│   └── binary_protocol.h/cpp # Optimized binary messages
├── mqtt/
│   └── mqtt_client.h/cpp     # MQTT client wrapper
├── gateway/
│   └── gateway.h/cpp         # Mesh-MQTT bridge logic
└── display/
    └── display_handler.h/cpp # OLED display handler

lib/
└── esp-mesh-lite-arduino/    # ESP-Mesh-Lite Arduino library (v1.1.0)
```

## Setup

1. Copy `.env.example` to `.env`
2. Configure your settings:
   ```
   WIFI_SSID="YourWiFiSSID"
   WIFI_PASSWORD="YourWiFiPassword"
   MQTT_BROKER="broker.hivemq.com"
   MQTT_PORT=1883
   ```

3. Build and upload:
   ```bash
   pio run -t upload && pio device monitor
   ```

4. OTA (optional):
   - Ensure the OTA partition table is active (`partitions_ota.csv` is already set in `platformio.ini`).
   - Host the built firmware binary (e.g., `.pio/build/<env>/firmware.bin`) on an HTTP/HTTPS server.

## MQTT Topics

Device-centric structure: `mesh-lite/{device_id}/xxx`

| Topic | Direction | Description |
|-------|-----------|-------------|
| `mesh-lite/{id}/status` | Publish | Node status (unified format) |
| `mesh-lite/{id}/data` | Publish | Data from node |
| `mesh-lite/{id}/cmd` | Subscribe | Command to specific node |
| `mesh-lite/broadcast` | Subscribe | Broadcast to all nodes |
| `mesh-lite/{id}/ota/cmd` | Subscribe | OTA command to device (root nodes only) |
| `mesh-lite/{id}/ota/status` | Publish | OTA status/progress |
| `mesh-lite/broadcast/ota` | Subscribe | Broadcast OTA command (root nodes only) |

### Status Payload

All nodes publish the same status format:

```json
{
  "id": "AABBCCDDEEFF",
  "level": 1,
  "root": true,
  "heap": 234567,
  "rssi": -45
}
```

| Field | Description |
|-------|-------------|
| `id` | Device MAC address (12 hex chars) |
| `level` | Mesh level (1 = root, 2+ = child) |
| `root` | Whether node is connected to router |
| `heap` | Free heap memory (bytes) |
| `rssi` | WiFi signal strength (dBm) |

## OTA (Over-The-Air) Updates

OTA updates are triggered via MQTT and executed using ESP-IDF OTA (HTTP/HTTPS).
Only nodes that are connected to the router and MQTT (root nodes) can receive OTA commands.

### OTA Command Payloads

Publish to `mesh-lite/{id}/ota/cmd` or `mesh-lite/broadcast/ota`:

**Start**
```json
{
  "action": "start",
  "url": "https://example.com/firmware.bin",
  "version": "1.1.1",
  "sha256": "64-hex-optional",
  "size": 123456
}
```

**Cancel**
```json
{ "action": "cancel" }
```

**Status**
```json
{ "action": "status" }
```

Notes:
- `url` is required for `start`.
- `version` is optional and reported in OTA status.
- `size` is optional and helps progress reporting when the server does not provide length.
- `sha256` is accepted for future validation but is not enforced yet.

### OTA Status Payload

Published to `mesh-lite/{id}/ota/status`:

```json
{
  "state": "downloading",
  "progress": 42,
  "bytes_written": 53248,
  "bytes_total": 126976,
  "version": "1.1.1"
}
```

States: `idle`, `starting`, `downloading`, `writing`, `verifying`, `complete`, `failed`, `cancelled`
Errors (only on failure): `invalid_url`, `connection_failed`, `http_error`, `timeout`,
`checksum_mismatch`, `signature_invalid`, `write_failed`, `partition_error`,
`version_invalid`, `cancelled`, `unknown`

## Configuration

Key settings in `config.h`:

```cpp
// Mesh Mode
#define MESH_ROUTER_FIRST 1           // 1 = multi-root, 0 = single-root
#define MESH_ROUTER_RSSI_THRESHOLD -75 // dBm, fallback threshold

// Topology Optimization
#define MESH_FUSION_START_SEC 30      // Start fusion after boot
#define MESH_FUSION_INTERVAL_SEC 60   // Check interval

// Reconnection Timing
#define MESH_RECONNECT_PARENT_INTERVAL 3  // Seconds between attempts
#define MESH_RECONNECT_PARENT_COUNT 2     // Attempts before scanning
#define MESH_RECONNECT_SCAN_INTERVAL 5    // Scan interval

// Status Reporting
#define STATUS_INTERVAL_MS 10000      // Status publish interval

// OTA
#define OTA_ENABLED 1                 // 0 = disabled, 1 = enabled
#define OTA_SECURITY_LEVEL OTA_SECURITY_HTTPS  // HTTPS (default) or OTA_SECURITY_NONE for HTTP
#define OTA_TIMEOUT_SEC 300           // OTA download timeout
#define OTA_PROGRESS_INTERVAL_MS 5000 // Status update interval during OTA
```

## Binary Protocol

For bandwidth-constrained scenarios, child nodes use binary protocol:

| Message Type | Size | Fields |
|--------------|------|--------|
| Status | 16 bytes | type, mac[6], seq, level, heap, rssi |
| Command | 11+ bytes | type, mac[6], seq, cmdType, dataLen, data[] |
| Data | 12+ bytes | type, mac[6], seq, dataLen, data[] |

Binary messages are base64-encoded for mesh transport, then decoded and converted to JSON for MQTT.

## Requirements

- PlatformIO with [pioarduino](https://github.com/pioarduino/platform-espressif32) platform (ESP-IDF 5.x)
- ESP32, ESP32-S2, ESP32-S3, ESP32-C3, or ESP32-C6
- OTA requires an OTA partition table (this repo uses `partitions_ota.csv`)

## License

Apache-2.0
