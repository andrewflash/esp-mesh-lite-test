# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.2.0] - 2026-02-03

### Added

- OTA cancel command support and status reporting.
- Firmware version, parent, and PHY included in status payloads.
- OTA handler module and OTA partition layout for robust updates.

### Changed

- Binary status protocol to include firmware version.
- Gateway status publishing to include firmware version and PHY.

## [1.1.0] - 2025-01-31

### Added

- **OLED Display Support**
  - Configurable display handler for SSD1306 and SH1106 OLED displays
  - Support for 128x64 and 128x32 display sizes
  - Configurable I2C pins (SDA, SCL) and address
  - Status display showing device ID, mesh level, MQTT status, RSSI, and heap
  - Progress bar and message display methods
  - Conditional compilation: disable with `DISPLAY_ENABLED=0`

- **Watchdog Timer**
  - Auto-restart on system hang via ESP32 Task Watchdog
  - Configurable timeout via `WDT_TIMEOUT_SEC` (default: 30s)
  - Triggers panic and restart if mesh initialization fails

### Changed

- **Code Refactoring**
  - Extracted gateway logic into separate `gateway/` module (SOLID principles)
  - Simplified `main.cpp` for setup and orchestration only
  - MQTT reconnection now throttled via `MQTT_RECONNECT_INTERVAL_MS` (5s)

## [1.0.1] - 2025-01-31

### Fixed

- **Network Failure Fallback**
  - Automatic switch to mesh-only mode after consecutive MQTT/network failures
  - Configurable failure threshold via `MESH_MAX_ROUTER_FAILURES` (default: 5)
  - Node joins other mesh nodes as child when router connection fails
  - Failure counter resets on successful network operation

### Changed

- Renamed `MESH_MAX_ROUTER_AUTH_FAILURES` to `MESH_MAX_ROUTER_FAILURES`
- Network failure detection now uses application-level tracking (MQTT failures)
  instead of WiFi event-based tracking for better compatibility with ESP-Mesh-Lite

## [1.0.0] - 2025-01-31

### Added

- **Multi-Root Mesh Topology**
  - Router-first networking mode (`MESH_ROUTER_FIRST`)
  - Configurable RSSI threshold for mesh fallback
  - Fusion configuration for topology optimization
  - Reconnection interval configuration

- **MQTT Gateway**
  - Device-centric topic structure: `mesh-lite/{device_id}/xxx`
  - Unified status topic for all nodes (root and children)
  - Unified data topic for forwarding child data
  - Command topics for unicast and broadcast
  - Auto-reconnection to MQTT broker

- **Binary Protocol**
  - Compact 16-byte status messages (vs ~60 bytes JSON)
  - Base64 encoding for mesh transport
  - Message types: Status, Command, Data, Ack
  - Sequence numbering for tracking

- **Typed Messaging**
  - Request-response pattern with message type matching
  - Automatic retry with configurable attempts
  - Separate handlers for JSON and binary messages

- **Configuration**
  - Externalized settings via `config.h`
  - Environment variable support via `.env` file
  - Build-time configuration with PlatformIO

### Project Structure

```
src/
├── main.cpp              # Application entry point
├── config.h              # Configuration
├── mesh/
│   ├── mesh_handler.h/cpp    # Mesh communication wrapper
│   └── binary_protocol.h/cpp # Binary message protocol
├── mqtt/
│   └── mqtt_client.h/cpp     # MQTT client wrapper
├── gateway/
│   └── gateway.h/cpp         # Mesh-MQTT bridge logic
└── display/
    └── display_handler.h/cpp # OLED display handler

lib/
└── esp-mesh-lite-arduino/    # ESP-Mesh-Lite Arduino library (v1.1.0)
```

### MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `mesh-lite/{id}/status` | Publish | Node status |
| `mesh-lite/{id}/data` | Publish | Node data |
| `mesh-lite/{id}/cmd` | Subscribe | Command to node |
| `mesh-lite/broadcast` | Subscribe | Broadcast to all |

### Status Payload Format

```json
{
  "id": "AABBCCDDEEFF",
  "level": 1,
  "root": true,
  "heap": 234567,
  "rssi": -45
}
```

[1.2.0]: https://github.com/andrewflash/esp-mesh-lite-test/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/andrewflash/esp-mesh-lite-test/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/andrewflash/esp-mesh-lite-test/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/andrewflash/esp-mesh-lite-test/releases/tag/v1.0.0
