# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
├── main.cpp              # Gateway logic
├── config.h              # Configuration
├── mesh/
│   ├── mesh_handler.h/cpp    # Mesh communication wrapper
│   └── binary_protocol.h/cpp # Binary message protocol
└── mqtt/
    └── mqtt_client.h/cpp     # MQTT client wrapper

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

[1.0.0]: https://github.com/andrewflash/esp-mesh-lite-test/releases/tag/v1.0.0
