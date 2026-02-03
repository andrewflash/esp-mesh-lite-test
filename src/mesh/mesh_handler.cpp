#include "mesh_handler.h"
#include "../config.h"
#include <cJSON.h>
#include <esp_event.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <esp_mesh_lite.h>
#include <time.h>
#include <sys/time.h>

// Get WiFi RSSI using ESP-IDF API (works with mesh-lite)
static int8_t getWiFiRSSI()
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

MeshHandler* MeshHandler::_instance = nullptr;
MeshHandler meshHandler;

MeshHandler::MeshHandler()
    : _eventCallback(nullptr)
    , _messageCallback(nullptr)
    , _binaryCallback(nullptr)
    , _connectionFailures(0)
    , _meshOnlyMode(false)
    , _wifiDisconnects(0)
    , _levelChangeTime(0)
    , _lastLevel(0)
{
    _instance = this;
    memset(_mac, 0, sizeof(_mac));
}

bool MeshHandler::isChannelReady()
{
    uint8_t currentLevel = getLevel();

    // Track level changes
    if (currentLevel != _lastLevel) {
        _lastLevel = currentLevel;
        _levelChangeTime = millis();
        Serial.printf("[MESH] Level changed to %d, waiting for channel...\n", currentLevel);
    }

    // Not connected = not ready
    if (currentLevel == 0) {
        return false;
    }

    // Wait for TCP channel to establish after WiFi connects
    if (millis() - _levelChangeTime < CHANNEL_READY_DELAY_MS) {
        return false;
    }

    return true;
}

void MeshHandler::generateDeviceId()
{
    esp_read_mac(_mac, ESP_MAC_WIFI_STA);
    char id[13];
    snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X",
             _mac[0], _mac[1], _mac[2], _mac[3], _mac[4], _mac[5]);
    _deviceId = String(id);
}

void MeshHandler::meshEventHandler(esp_event_base_t base, int32_t id, void* data)
{
    if (_instance && _instance->_eventCallback) {
        _instance->_eventCallback(id);
    }
}

// Application-level network failure tracking
// Call this when network operations (DNS, MQTT) fail while node is root
void MeshHandler::reportNetworkFailure()
{
    if (_meshOnlyMode) return;  // Already in mesh-only mode

    _connectionFailures++;
    Serial.printf("[MESH] Network failure #%d/%d\n",
                  _connectionFailures, MESH_MAX_ROUTER_FAILURES);

    // Check if we should switch to mesh-only mode
    if (_connectionFailures >= MESH_MAX_ROUTER_FAILURES) {
        switchToMeshOnlyMode();
    }
}

// Call this when network operation succeeds
void MeshHandler::reportNetworkSuccess()
{
    if (_connectionFailures > 0) {
        Serial.println("[MESH] Network OK, reset failure counter");
        _connectionFailures = 0;
    }
}

void MeshHandler::switchToMeshOnlyMode()
{
    _meshOnlyMode = true;
    Serial.printf("[MESH] Switching to mesh-only mode after %d failures\n",
                  _connectionFailures);

    // Disable router-first mode, force mesh mode
    _mesh.setNetworkingMode(false, MESH_ROUTER_RSSI_THRESHOLD);

    // Use longer fusion interval to periodically retry router connection
    // This allows recovery when router comes back online
    _mesh.setFusionConfig(60, MESH_FUSION_RECOVERY_SEC);

    // Keep router credentials - allows recovery when router returns
    // The mesh-only networking mode will prioritize mesh parents over router

    Serial.printf("[MESH] Now scanning for mesh parents (will retry router every %ds)\n",
                  MESH_FUSION_RECOVERY_SEC);
}

void MeshHandler::checkRecoveryFromMeshOnly()
{
    // If we were in mesh-only mode and became root, router is back
    if (_meshOnlyMode && isRoot()) {
        _meshOnlyMode = false;
        _connectionFailures = 0;
        _wifiDisconnects = 0;

        // Restore normal fusion interval
        _mesh.setFusionConfig(MESH_FUSION_START_SEC, MESH_FUSION_INTERVAL_SEC);

        // Re-enable router-first mode
        _mesh.setNetworkingMode(MESH_ROUTER_FIRST, MESH_ROUTER_RSSI_THRESHOLD);

        Serial.println("[MESH] Recovered from mesh-only mode - router available");
    }
}

void MeshHandler::wifiEventHandler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    if (!_instance) return;

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        if (_instance->_wifiDisconnects > 0) {
            Serial.println("[MESH] STA got IP, reset WiFi disconnect counter");
            _instance->_wifiDisconnects = 0;
        }
        return;
    }

    if (base != WIFI_EVENT || id != WIFI_EVENT_STA_DISCONNECTED) {
        return;
    }

    // In mesh-only mode, WiFi disconnects are expected during mesh operation
    // Don't count them or log spam
    if (_instance->_meshOnlyMode) {
        return;
    }

    wifi_event_sta_disconnected_t* event = static_cast<wifi_event_sta_disconnected_t*>(data);
    uint16_t reason = event ? event->reason : 0;
    _instance->_wifiDisconnects++;

    Serial.printf("[MESH] WiFi disconnected (reason=%u) %u/%u\n",
                  reason, _instance->_wifiDisconnects, MESH_MAX_WIFI_FAILURES);

    if (_instance->_wifiDisconnects >= MESH_MAX_WIFI_FAILURES) {
        _instance->_wifiDisconnects = 0;
        _instance->switchToMeshOnlyMode();
    }
}

cJSON* MeshHandler::meshMessageHandler(cJSON* payload, uint32_t seq)
{
    if (!_instance || !_instance->_messageCallback || !payload) {
        return nullptr;
    }

    // Extract message fields
    cJSON* toField = cJSON_GetObjectItem(payload, "to");
    cJSON* fromField = cJSON_GetObjectItem(payload, "from");
    cJSON* dataField = cJSON_GetObjectItem(payload, "data");

    if (!fromField || !dataField) {
        return nullptr;
    }

    const char* to = toField ? toField->valuestring : nullptr;
    const char* from = fromField->valuestring;
    const char* data = dataField->valuestring;

    // Check if message is for us (unicast) or broadcast
    if (to && strcmp(to, "*") != 0) {
        // Unicast: check if we're the target
        if (strcmp(to, _instance->_deviceId.c_str()) != 0) {
            return nullptr;  // Not for us, ignore
        }
    }

    // Deliver message
    _instance->_messageCallback(from, data);

    return nullptr;
}

bool MeshHandler::begin(const char* routerSsid, const char* routerPassword,
                        const char* softapSsid, const char* softapPassword,
                        uint8_t meshId)
{
    generateDeviceId();

    // Save credentials for recovery after mesh-only mode
    _savedRouterSsid = routerSsid;
    _savedRouterPassword = routerPassword;

    _mesh.onEvent(meshEventHandler);
    _mesh.setRouterCredentials(routerSsid, routerPassword);
    _mesh.setSoftAPCredentials(softapSsid, softapPassword);
    _mesh.setMeshId(meshId);

    bool ok = _mesh.begin();
    if (ok) {
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifiEventHandler, nullptr);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifiEventHandler, nullptr);
    }
    return ok;
}

void MeshHandler::start()
{
    // Router-first mode: each node tries router first, only joins mesh if unavailable
    // Multiple nodes can be root simultaneously when all have router connectivity
    // Must be called after begin() (init) but before start()
    _mesh.setNetworkingMode(MESH_ROUTER_FIRST, MESH_ROUTER_RSSI_THRESHOLD);

    // Fusion: periodically try to optimize topology (reconnect to router if available)
    _mesh.setFusionConfig(MESH_FUSION_START_SEC, MESH_FUSION_INTERVAL_SEC);

    // Reconnection: how quickly to fallback to mesh when router is lost
    _mesh.setReconnectInterval(
        MESH_RECONNECT_PARENT_INTERVAL,
        MESH_RECONNECT_PARENT_COUNT,
        MESH_RECONNECT_SCAN_INTERVAL
    );

    // WiFi protocol modes (LR mode extends range but only works between ESP32s)
    _mesh.setWiFiProtocol(MESH_WIFI_STA_PROTOCOL, MESH_WIFI_SOFTAP_PROTOCOL);

    _mesh.start();

    // Register message handlers
    _mesh.onMessage("msg", "msg_ack", meshMessageHandler);
    _mesh.onMessage("bin", "bin_ack", meshBinaryHandler);
}

uint8_t MeshHandler::getLevel()
{
    return _mesh.getLevel();
}

bool MeshHandler::isRoot()
{
    return _mesh.isRoot();
}

bool MeshHandler::isConnected()
{
    return _mesh.getLevel() > 0;
}

String MeshHandler::getDeviceId()
{
    return _deviceId;
}

uint8_t MeshHandler::getNegotiatedPhyMode()
{
    wifi_phy_mode_t mode;
    if (esp_wifi_sta_get_negotiated_phymode(&mode) == ESP_OK) {
        return (uint8_t)mode;
    }
    return 0;
}

void MeshHandler::initTimeSync()
{
    // Configure SNTP for NTP time synchronization
    configTime(TIME_ZONE_OFFSET, 0, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);
    Serial.println("[TIME] NTP sync started");
}

bool MeshHandler::isTimeSynced()
{
    time_t now;
    time(&now);
    // Time is valid if after Nov 2023 (Unix timestamp > 1700000000)
    return now > 1700000000;
}

uint32_t MeshHandler::getTimestamp()
{
    if (!isTimeSynced()) {
        return 0;
    }
    time_t now;
    time(&now);
    return (uint32_t)now;
}

void MeshHandler::syncTimeFromRoot(uint32_t timestamp)
{
    if (timestamp <= 1700000000) {
        return;  // Invalid timestamp
    }

    // Only sync if time differs by more than 2 seconds (avoid log spam)
    time_t current;
    time(&current);
    int32_t diff = (int32_t)timestamp - (int32_t)current;
    if (diff < -2 || diff > 2) {
        struct timeval tv = { .tv_sec = (time_t)timestamp, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        Serial.printf("[TIME] Synced from root: %lu (diff=%ld)\n",
                      (unsigned long)timestamp, (long)diff);
    }
}

bool MeshHandler::broadcastTimeSync(uint32_t timestamp)
{
    if (!isConnected() || timestamp == 0) {
        return false;
    }

    // Get own MAC for the status message
    uint8_t parentMac[6] = {0};
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        memcpy(parentMac, ap_info.bssid, 6);
    }

    uint8_t buffer[sizeof(StatusMsg)];
    size_t len = BinaryProtocol::createStatusMsg(
        buffer, sizeof(buffer),
        _mac,
        getLevel(),
        ESP.getFreeHeap(),
        getWiFiRSSI(),
        parentMac,
        getNegotiatedPhyMode(),
        timestamp,
        PROJECT_VERSION
    );

    if (len == 0) return false;

    bool result = sendBinaryToChildren(buffer, len);
    if (result) {
        Serial.printf("[TIME] Broadcast to children: %lu\n", (unsigned long)timestamp);
    }
    return result;
}

void MeshHandler::onEvent(MeshEventCallback callback)
{
    _eventCallback = callback;
}

void MeshHandler::onMessage(MeshMessageCallback callback)
{
    _messageCallback = callback;
}

void MeshHandler::onBinaryMessage(MeshBinaryCallback callback)
{
    _binaryCallback = callback;
}

cJSON* MeshHandler::meshBinaryHandler(cJSON* payload, uint32_t seq)
{
    if (!_instance || !payload) {
        return nullptr;
    }

    // Binary data is base64 encoded in the "b" field
    cJSON* bField = cJSON_GetObjectItem(payload, "b");
    if (!bField || !cJSON_IsString(bField)) {
        return nullptr;
    }

    // Decode base64 to binary
    const char* b64 = bField->valuestring;
    size_t b64Len = strlen(b64);
    size_t binLen = (b64Len * 3) / 4;  // Approximate decoded length
    uint8_t* binData = (uint8_t*)malloc(binLen);
    if (!binData) return nullptr;

    // Simple base64 decode
    static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t outIdx = 0;
    uint32_t accumulator = 0;
    int bits = 0;

    for (size_t i = 0; i < b64Len; i++) {
        char c = b64[i];
        if (c == '=') break;

        const char* p = strchr(b64chars, c);
        if (!p) continue;

        accumulator = (accumulator << 6) | (p - b64chars);
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            binData[outIdx++] = (accumulator >> bits) & 0xFF;
        }
    }

    // Deliver to callback
    if (_instance->_binaryCallback && outIdx > 0) {
        _instance->_binaryCallback(binData, outIdx);
    }

    free(binData);
    return nullptr;
}

// Uplink: child -> root
bool MeshHandler::sendToRoot(const char* data)
{
    if (!isConnected() || isRoot() || !isChannelReady()) {
        return false;
    }

    cJSON* payload = cJSON_CreateObject();
    if (!payload) return false;

    cJSON_AddStringToObject(payload, "from", _deviceId.c_str());
    cJSON_AddStringToObject(payload, "data", data);

    // Use typed message API so root's onMessage("msg", ...) handler receives it
    bool result = _mesh.sendTypedToRoot("msg", "msg_ack", payload);

    cJSON_Delete(payload);
    return result;
}

// Downlink: unicast to specific node (logical unicast via broadcast)
bool MeshHandler::sendToNode(const char* targetId, const char* data)
{
    if (!isConnected()) {
        return false;
    }

    cJSON* payload = cJSON_CreateObject();
    if (!payload) return false;

    cJSON_AddStringToObject(payload, "to", targetId);  // Target device
    cJSON_AddStringToObject(payload, "from", _deviceId.c_str());
    cJSON_AddStringToObject(payload, "data", data);

    // Use typed message API
    bool result = _mesh.sendTypedToChildren("msg", "msg_ack", payload);

    cJSON_Delete(payload);
    return result;
}

// Downlink: broadcast to all nodes
bool MeshHandler::broadcast(const char* data)
{
    if (!isConnected()) {
        return false;
    }

    cJSON* payload = cJSON_CreateObject();
    if (!payload) return false;

    cJSON_AddStringToObject(payload, "to", "*");  // Broadcast marker
    cJSON_AddStringToObject(payload, "from", _deviceId.c_str());
    cJSON_AddStringToObject(payload, "data", data);

    // Use typed message API
    bool result = _mesh.sendTypedToChildren("msg", "msg_ack", payload);

    cJSON_Delete(payload);
    return result;
}

// Helper: Base64 encode binary data
static size_t base64Encode(const uint8_t* input, size_t inputLen, char* output, size_t outputLen)
{
    static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t needed = ((inputLen + 2) / 3) * 4 + 1;
    if (outputLen < needed) return 0;

    size_t outIdx = 0;
    for (size_t i = 0; i < inputLen; i += 3) {
        uint32_t n = ((uint32_t)input[i]) << 16;
        if (i + 1 < inputLen) n |= ((uint32_t)input[i + 1]) << 8;
        if (i + 2 < inputLen) n |= input[i + 2];

        output[outIdx++] = b64chars[(n >> 18) & 0x3F];
        output[outIdx++] = b64chars[(n >> 12) & 0x3F];
        output[outIdx++] = (i + 1 < inputLen) ? b64chars[(n >> 6) & 0x3F] : '=';
        output[outIdx++] = (i + 2 < inputLen) ? b64chars[n & 0x3F] : '=';
    }
    output[outIdx] = '\0';
    return outIdx;
}

// Binary uplink: child -> root
bool MeshHandler::sendBinaryToRoot(const uint8_t* data, size_t len)
{
    if (!isConnected() || isRoot() || !isChannelReady() || len == 0) {
        return false;
    }

    // Base64 encode the binary data
    size_t b64Len = ((len + 2) / 3) * 4 + 1;
    char* b64 = (char*)malloc(b64Len);
    if (!b64) return false;

    base64Encode(data, len, b64, b64Len);

    cJSON* payload = cJSON_CreateObject();
    if (!payload) {
        free(b64);
        return false;
    }

    cJSON_AddStringToObject(payload, "b", b64);  // "b" for binary

    bool result = _mesh.sendTypedToRoot("bin", "bin_ack", payload);

    cJSON_Delete(payload);
    free(b64);
    return result;
}

// Binary downlink: root -> children
bool MeshHandler::sendBinaryToChildren(const uint8_t* data, size_t len)
{
    if (!isConnected() || len == 0) {
        return false;
    }

    // Base64 encode the binary data
    size_t b64Len = ((len + 2) / 3) * 4 + 1;
    char* b64 = (char*)malloc(b64Len);
    if (!b64) return false;

    base64Encode(data, len, b64, b64Len);

    cJSON* payload = cJSON_CreateObject();
    if (!payload) {
        free(b64);
        return false;
    }

    cJSON_AddStringToObject(payload, "b", b64);

    bool result = _mesh.sendTypedToChildren("bin", "bin_ack", payload);

    cJSON_Delete(payload);
    free(b64);
    return result;
}

// Convenience: Send binary status message to root
bool MeshHandler::sendStatusToRoot()
{
    if (!isConnected() || isRoot()) {
        return false;
    }

    // Get parent MAC (STA MAC derived from BSSID)
    uint8_t parentMac[6] = {0};
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        memcpy(parentMac, ap_info.bssid, 6);
        // Convert SoftAP BSSID to STA MAC: clear locally administered bit (bit 1 of first byte)
        parentMac[0] &= 0xFD;
    }

    uint8_t buffer[sizeof(StatusMsg)];
    size_t len = BinaryProtocol::createStatusMsg(
        buffer, sizeof(buffer),
        _mac,
        getLevel(),
        ESP.getFreeHeap(),
        getWiFiRSSI(),
        parentMac,
        getNegotiatedPhyMode(),
        getTimestamp(),
        PROJECT_VERSION
    );

    if (len == 0) return false;

    return sendBinaryToRoot(buffer, len);
}

// Get parent device ID for tree visualization
// Returns router BSSID if root, mesh parent STA MAC if child
String MeshHandler::getParentId()
{
    if (!isConnected()) {
        return "";
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return "";
    }

    uint8_t parentMac[6];
    memcpy(parentMac, ap_info.bssid, 6);

    // If not root, convert parent's SoftAP BSSID to STA MAC
    // ESP32 SoftAP MAC = STA MAC with bit 1 of first byte set (locally administered)
    // Clear bit 1 to get STA MAC
    if (!isRoot()) {
        parentMac[0] &= 0xFD;
    }

    char id[13];
    snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X",
             parentMac[0], parentMac[1], parentMac[2],
             parentMac[3], parentMac[4], parentMac[5]);
    return String(id);
}
