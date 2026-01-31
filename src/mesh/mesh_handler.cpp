#include "mesh_handler.h"
#include "../config.h"
#include <cJSON.h>
#include <esp_mac.h>
#include <esp_wifi.h>

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
{
    _instance = this;
    memset(_mac, 0, sizeof(_mac));
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

    _mesh.onEvent(meshEventHandler);
    _mesh.setRouterCredentials(routerSsid, routerPassword);
    _mesh.setSoftAPCredentials(softapSsid, softapPassword);
    _mesh.setMeshId(meshId);

    return _mesh.begin();
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
    if (!isConnected() || isRoot()) {
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
    if (!isConnected() || isRoot() || len == 0) {
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

    uint8_t buffer[sizeof(StatusMsg)];
    size_t len = BinaryProtocol::createStatusMsg(
        buffer, sizeof(buffer),
        _mac,
        getLevel(),
        ESP.getFreeHeap(),
        getWiFiRSSI()
    );

    if (len == 0) return false;

    return sendBinaryToRoot(buffer, len);
}
