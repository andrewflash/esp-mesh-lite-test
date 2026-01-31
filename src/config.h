#pragma once

// Project Version
#define PROJECT_VERSION "1.1.0"

// Build-time configuration from .env
#ifndef WIFI_SSID
#define WIFI_SSID "DefaultSSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "DefaultPassword"
#endif
#ifndef MESH_SOFTAP_SSID
#define MESH_SOFTAP_SSID "ESP"
#endif
#ifndef MESH_SOFTAP_PASSWORD
#define MESH_SOFTAP_PASSWORD "12345678"
#endif
#ifndef MESH_ID
#define MESH_ID 77
#endif

// Mesh Networking Mode
#ifndef MESH_ROUTER_FIRST
#define MESH_ROUTER_FIRST 1              // 1 = router-first (multi-root), 0 = mesh mode
#endif
#ifndef MESH_ROUTER_RSSI_THRESHOLD
#define MESH_ROUTER_RSSI_THRESHOLD -75   // dBm, fallback to mesh if router signal weaker
#endif

// Mesh Fusion (topology optimization)
#ifndef MESH_FUSION_START_SEC
#define MESH_FUSION_START_SEC 30         // seconds after boot to start fusion
#endif
#ifndef MESH_FUSION_INTERVAL_SEC
#define MESH_FUSION_INTERVAL_SEC 60      // seconds between fusion checks
#endif

// Mesh Reconnection (when disconnected from parent)
#ifndef MESH_RECONNECT_PARENT_INTERVAL
#define MESH_RECONNECT_PARENT_INTERVAL 3  // seconds between reconnect attempts to current parent
#endif
#ifndef MESH_RECONNECT_PARENT_COUNT
#define MESH_RECONNECT_PARENT_COUNT 2     // max attempts before scanning for new parent
#endif
#ifndef MESH_RECONNECT_SCAN_INTERVAL
#define MESH_RECONNECT_SCAN_INTERVAL 5    // seconds between scans for new parent
#endif

// Network Failure Fallback (switches to mesh-only mode after N failures)
#ifndef MESH_MAX_ROUTER_FAILURES
#define MESH_MAX_ROUTER_FAILURES 5        // max MQTT/network failures before mesh fallback
#endif

// MQTT Configuration
#ifndef MQTT_BROKER
#define MQTT_BROKER "broker.hivemq.com"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif

// MQTT Topics (device-centric structure)
#define MQTT_TOPIC_PREFIX "mesh-lite"
//   {prefix}/{device_id}/status  - node status: {"id":"...","level":N,"root":bool,"heap":N,"rssi":N}
//   {prefix}/{device_id}/data    - node data:   {"id":"...","data":{...}}
//   {prefix}/{device_id}/cmd     - command to node (downlink)
//   {prefix}/broadcast           - broadcast to all nodes

// Watchdog
#ifndef WDT_TIMEOUT_SEC
#define WDT_TIMEOUT_SEC 30                // Watchdog timeout in seconds
#endif

// Intervals
#define STATUS_INTERVAL_MS 10000
#define MQTT_RECONNECT_INTERVAL_MS 5000

// OLED Display Configuration
#ifndef DISPLAY_ENABLED
#define DISPLAY_ENABLED 1             // 0 = disabled, 1 = enabled
#endif
#ifndef DISPLAY_TYPE
#define DISPLAY_TYPE DISPLAY_SSD1306  // DISPLAY_SSD1306 or DISPLAY_SH1106
#endif
#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH 128
#endif
#ifndef DISPLAY_HEIGHT
#define DISPLAY_HEIGHT 64             // 64 or 32
#endif
#ifndef DISPLAY_SDA_PIN
#define DISPLAY_SDA_PIN 21            // I2C SDA pin
#endif
#ifndef DISPLAY_SCL_PIN
#define DISPLAY_SCL_PIN 22            // I2C SCL pin
#endif
#ifndef DISPLAY_I2C_ADDR
#define DISPLAY_I2C_ADDR 0x3C         // 0x3C or 0x3D
#endif
#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 0            // 0, 1, 2, or 3 (90° increments)
#endif
#ifndef DISPLAY_UPDATE_MS
#define DISPLAY_UPDATE_MS 1000        // Display refresh interval
#endif

// Display type constants
#define DISPLAY_SSD1306 0
#define DISPLAY_SH1106  1
