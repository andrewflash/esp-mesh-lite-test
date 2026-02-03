#pragma once

// Project Version
#define PROJECT_VERSION "1.1.0"

// Development Options
#ifndef ERASE_NVS_ON_BOOT
#define ERASE_NVS_ON_BOOT 0               // 1 = erase NVS on boot (clears stored WiFi credentials)
#endif

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
// WiFi Protocol Modes (use MeshLiteWiFiProtocol enum values, can be OR'd)
// STA: For connecting to router or mesh parent
// SoftAP: For mesh children to connect
// Available modes: MESH_WIFI_PROTOCOL_11B, _11G, _11N, _LR, _BGN, _BGNLR, _LR_ONLY
// Note: LR (Long Range) only works between ESP32 devices, not with standard routers
#ifndef MESH_WIFI_STA_PROTOCOL
#define MESH_WIFI_STA_PROTOCOL MESH_WIFI_PROTOCOL_BGNLR  // B/G/N/LR for router + mesh
#endif
#ifndef MESH_WIFI_SOFTAP_PROTOCOL
#define MESH_WIFI_SOFTAP_PROTOCOL MESH_WIFI_PROTOCOL_BGNLR  // B/G/N/LR for mesh
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
#ifndef MESH_FUSION_RECOVERY_SEC
#define MESH_FUSION_RECOVERY_SEC 300     // seconds between router retry in mesh-only mode (5 min)
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
#ifndef MESH_MAX_WIFI_FAILURES
#define MESH_MAX_WIFI_FAILURES 5          // max WiFi disconnects before forcing mesh-only
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
#ifndef MQTT_CLIENT_PREFIX
#define MQTT_CLIENT_PREFIX "mesh"         // Client ID = {prefix}-{MAC}
#endif
#ifndef MQTT_BUFFER_SIZE
#define MQTT_BUFFER_SIZE 2048             // Increase to allow OTA payloads with sha/size
#endif

// MQTT Topics (device-centric structure)
#define MQTT_TOPIC_PREFIX "mesh-lite"
//   {prefix}/{device_id}/status    - node status: {"id":"...","level":N,"root":bool,"heap":N,"rssi":N,"parent":"..."}
//   {prefix}/{device_id}/data      - node data:   {"id":"...","data":{...}}
//   {prefix}/{device_id}/cmd       - command to node (downlink)
//   {prefix}/broadcast             - broadcast to all nodes

// Time Sync (NTP)
#ifndef NTP_SERVER_PRIMARY
#define NTP_SERVER_PRIMARY "pool.ntp.org"
#endif
#ifndef NTP_SERVER_SECONDARY
#define NTP_SERVER_SECONDARY "time.nist.gov"
#endif
#ifndef TIME_ZONE_OFFSET
#define TIME_ZONE_OFFSET 0            // UTC offset in seconds (0 = UTC)
#endif

// OTA (Over-The-Air) Update Configuration
#ifndef OTA_ENABLED
#define OTA_ENABLED 1                 // 0 = disabled, 1 = enabled
#endif

// OTA Security Levels
#define OTA_SECURITY_NONE       0     // HTTP only (development)
#define OTA_SECURITY_HTTPS      1     // HTTPS with CA bundle
#define OTA_SECURITY_SIGNED     2     // HTTPS + RSA signature verification

#ifndef OTA_SECURITY_LEVEL
#define OTA_SECURITY_LEVEL OTA_SECURITY_HTTPS  // Default: HTTPS
#endif

// Derived security flags
#if OTA_SECURITY_LEVEL >= OTA_SECURITY_HTTPS
#define OTA_USE_HTTPS 1
#else
#define OTA_USE_HTTPS 0
#endif

#if OTA_SECURITY_LEVEL >= OTA_SECURITY_SIGNED
#define OTA_VERIFY_SIGNATURE 1
#else
#define OTA_VERIFY_SIGNATURE 0
#endif

// OTA Timing
#ifndef OTA_TIMEOUT_SEC
#define OTA_TIMEOUT_SEC 300           // 5 minutes max for OTA download
#endif
#ifndef OTA_PROGRESS_INTERVAL_MS
#define OTA_PROGRESS_INTERVAL_MS 5000 // Status update interval during OTA
#endif

// OTA Server (optional - can be overridden via MQTT command)
#ifndef OTA_SERVER_URL
#define OTA_SERVER_URL ""             // Default OTA server base URL
#endif

// OTA MQTT Topics
// {prefix}/{device_id}/ota/cmd      <- Command to device (subscribe)
// {prefix}/{device_id}/ota/status   -> Status from device (publish)
// {prefix}/broadcast/ota            <- Broadcast to all (subscribe)

// Watchdog
#ifndef WDT_TIMEOUT_SEC
#define WDT_TIMEOUT_SEC 30                // Watchdog timeout in seconds
#endif

// Intervals (optimized for 50+ nodes)
#define STATUS_INTERVAL_MS 30000          // Node status publish interval
#define MQTT_RECONNECT_INTERVAL_MS 5000   // MQTT reconnection throttle

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
