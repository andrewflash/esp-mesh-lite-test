#include "gateway.h"
#include <esp_netif.h>
#include <cJSON.h>

Gateway* Gateway::_instance = nullptr;
Gateway gateway;

Gateway::Gateway()
    : _lastPublishTime(0)
    , _lastMqttAttempt(0)
    , _mqttSubscribed(false)
    , _ntpInitialized(false)
{
    _instance = this;
}

int8_t Gateway::getWiFiRSSI()
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

bool Gateway::isWiFiReady()
{
    // Check if WiFi STA has a valid IP address (not 0.0.0.0)
    esp_netif_ip_info_t ip_info;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        return ip_info.ip.addr != 0;
    }
    return false;
}

void Gateway::begin()
{
    // Register mesh callbacks
    meshHandler.onEvent(onMeshEvent);
    meshHandler.onMessage(onMeshMessage);
    meshHandler.onBinaryMessage(onBinaryMessage);

    // Register MQTT callback
    mqtt.setCallback(onMqttMessage);

    _lastPublishTime = millis();
}

void Gateway::loop()
{
    if (meshHandler.isRoot()) {
        if (!mqtt.isConnected()) {
            // Wait for WiFi to be ready (has valid IP) before attempting MQTT
            if (!isWiFiReady()) {
                return;
            }

            // Initialize NTP when WiFi becomes ready (root node only)
            if (!_ntpInitialized) {
                MeshHandler::initTimeSync();
                _ntpInitialized = true;
            }

            // Throttle reconnection attempts
            if (millis() - _lastMqttAttempt >= MQTT_RECONNECT_INTERVAL_MS) {
                _lastMqttAttempt = millis();
                if (!mqtt.connect()) {
                    meshHandler.reportNetworkFailure();
                } else {
                    meshHandler.reportNetworkSuccess();
                }
            }
        } else {
            mqtt.loop();
            subscribeToCommands();

            if (millis() - _lastPublishTime >= STATUS_INTERVAL_MS) {
                _lastPublishTime = millis();
                publishRootStatus();
            }
        }
    } else if (meshHandler.isConnected() && meshHandler.isChannelReady()) {
        if (millis() - _lastPublishTime >= STATUS_INTERVAL_MS) {
            _lastPublishTime = millis();
            sendStatusToRoot();
        }
    }
}

void Gateway::publishStatus(const char* deviceId, uint8_t level, bool isRoot,
                            uint32_t heap, int8_t rssi, const char* parentId,
                            uint8_t phy, uint32_t timestamp)
{
    if (!mqtt.isConnected()) return;

    cJSON* json = cJSON_CreateObject();
    if (!json) return;

    if (timestamp > 0) {
        cJSON_AddNumberToObject(json, "ts", timestamp);
    }
    cJSON_AddStringToObject(json, "id", deviceId);
    cJSON_AddNumberToObject(json, "level", level);
    cJSON_AddBoolToObject(json, "root", isRoot);
    cJSON_AddNumberToObject(json, "heap", heap);
    cJSON_AddNumberToObject(json, "rssi", rssi);
    cJSON_AddStringToObject(json, "parent", parentId);
    cJSON_AddNumberToObject(json, "phy", phy);

    char* payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!payload) return;

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/%s/status", MQTT_TOPIC_PREFIX, deviceId);

    if (mqtt.publish(topic, payload)) {
        Serial.printf("[MQTT] Status: %s\n", topic);
    }

    cJSON_free(payload);
}

void Gateway::publishData(const char* deviceId, const char* data)
{
    if (!mqtt.isConnected()) return;

    cJSON* json = cJSON_CreateObject();
    if (!json) return;

    cJSON_AddStringToObject(json, "id", deviceId);

    // Parse data as JSON if possible, otherwise add as string
    cJSON* dataJson = cJSON_Parse(data);
    if (dataJson) {
        cJSON_AddItemToObject(json, "data", dataJson);
    } else {
        cJSON_AddStringToObject(json, "data", data);
    }

    char* payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!payload) return;

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/%s/data", MQTT_TOPIC_PREFIX, deviceId);

    if (mqtt.publish(topic, payload)) {
        Serial.printf("[MQTT] Data: %s\n", topic);
    }

    cJSON_free(payload);
}

void Gateway::publishRootStatus()
{
    uint32_t timestamp = MeshHandler::getTimestamp();

    // Publish to MQTT
    publishStatus(
        meshHandler.getDeviceId().c_str(),
        meshHandler.getLevel(),
        true,
        ESP.getFreeHeap(),
        getWiFiRSSI(),
        meshHandler.getParentId().c_str(),
        MeshHandler::getNegotiatedPhyMode(),
        timestamp
    );

    // Broadcast time to children (they sync from this)
    if (timestamp > 0) {
        meshHandler.broadcastTimeSync(timestamp);
    }
}

void Gateway::sendStatusToRoot()
{
#if USE_BINARY_STATUS
    if (meshHandler.sendStatusToRoot()) {
        Serial.printf("[MESH-BIN] Tx status to root (26 bytes)\n");
    } else {
        Serial.printf("[MESH-BIN] Tx status failed (level=%d, connected=%s)\n",
                      meshHandler.getLevel(),
                      meshHandler.isConnected() ? "yes" : "no");
    }
#else
    cJSON* json = cJSON_CreateObject();
    if (!json) return;

    cJSON_AddNumberToObject(json, "level", meshHandler.getLevel());
    cJSON_AddNumberToObject(json, "heap", ESP.getFreeHeap());

    char* payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!payload) return;

    if (meshHandler.sendToRoot(payload)) {
        Serial.printf("[MESH] Tx to root: %s\n", payload);
    }

    cJSON_free(payload);
#endif
}

void Gateway::subscribeToCommands()
{
    if (_mqttSubscribed) return;

    char topic[64];

    snprintf(topic, sizeof(topic), "%s/+/cmd", MQTT_TOPIC_PREFIX);
    mqtt.subscribe(topic);

    snprintf(topic, sizeof(topic), "%s/broadcast", MQTT_TOPIC_PREFIX);
    mqtt.subscribe(topic);

    _mqttSubscribed = true;
}

void Gateway::onMeshMessage(const char* from, const char* data)
{
    Serial.printf("[MESH] From %s: %s\n", from, data);

    if (!meshHandler.isRoot() || !_instance) {
        return;
    }

    // If this looks like a status JSON, publish to status topic
    cJSON* json = cJSON_Parse(data);
    if (json) {
        cJSON* type = cJSON_GetObjectItem(json, "type");
        cJSON* level = cJSON_GetObjectItem(json, "level");
        cJSON* heap = cJSON_GetObjectItem(json, "heap");
        cJSON* rssi = cJSON_GetObjectItem(json, "rssi");
        cJSON* parent = cJSON_GetObjectItem(json, "parent");
        cJSON* phy = cJSON_GetObjectItem(json, "phy");
        bool isStatus = type && cJSON_IsString(type) && strcmp(type->valuestring, "status") == 0;
        bool hasFields = level && heap && rssi;

        if (isStatus || hasFields) {
            cJSON* ts = cJSON_GetObjectItem(json, "ts");
            _instance->publishStatus(from,
                                     level ? (uint8_t)level->valueint : 0,
                                     false,
                                     heap ? (uint32_t)heap->valuedouble : 0,
                                     rssi ? (int8_t)rssi->valueint : 0,
                                     parent && cJSON_IsString(parent) ? parent->valuestring : "",
                                     phy ? (uint8_t)phy->valueint : 0,
                                     ts ? (uint32_t)ts->valuedouble : 0);
            cJSON_Delete(json);
            return;
        }

        cJSON_Delete(json);
    }

    _instance->publishData(from, data);
}

void Gateway::onBinaryMessage(const uint8_t* data, size_t len)
{
    if (len < sizeof(BinaryMsgHeader)) {
        Serial.printf("[MESH-BIN] Invalid message, len=%d\n", len);
        return;
    }

    BinaryMsgHeader header;
    BinaryProtocol::parseHeader(data, len, &header);

    char macStr[13];
    BinaryProtocol::macToString(header.mac, macStr, sizeof(macStr));

    Serial.printf("[MESH-BIN] From %s, type=%d, seq=%d, len=%d\n",
                  macStr, header.type, header.seq, len);

    switch (header.type) {
        case MSG_TYPE_STATUS: {
            if (len >= sizeof(StatusMsg)) {
                const StatusMsg* status = (const StatusMsg*)data;
                char parentStr[13];
                BinaryProtocol::macToString(status->parentMac, parentStr, sizeof(parentStr));

                Serial.printf("[MESH-BIN] Status: level=%d, heap=%lu, rssi=%d, parent=%s, phy=%d, ts=%lu\n",
                              status->level, status->heap, status->rssi, parentStr, status->phy,
                              (unsigned long)status->timestamp);

                if (meshHandler.isRoot() && _instance) {
                    _instance->publishStatus(macStr, status->level, false,
                                             status->heap, status->rssi, parentStr, status->phy,
                                             status->timestamp);
                } else if (!meshHandler.isRoot() && status->timestamp > 0) {
                    // Child node: sync time from upstream parent
                    meshHandler.syncTimeFromRoot(status->timestamp);
                }
            }
            break;
        }
        case MSG_TYPE_COMMAND:
            Serial.println("[MESH-BIN] Received command");
            break;
        case MSG_TYPE_DATA:
            Serial.println("[MESH-BIN] Received data");
            break;
        default:
            Serial.printf("[MESH-BIN] Unknown type: %d\n", header.type);
            break;
    }
}

void Gateway::onMeshEvent(int32_t eventId)
{
    switch (eventId) {
        case ESP_MESH_LITE_EVENT_CORE_STARTED:
            Serial.println("[MESH] Started");
            break;
        case ESP_MESH_LITE_EVENT_CORE_INHERITED_NET_SEGMENT_CHANGED:
            Serial.printf("[MESH] Level: %d (%s)\n",
                meshHandler.getLevel(),
                meshHandler.isRoot() ? "Root" : "Node");
            if (_instance) {
                _instance->_mqttSubscribed = false;
            }
            // Check if we recovered from mesh-only mode (became root again)
            meshHandler.checkRecoveryFromMeshOnly();
            break;
        case ESP_MESH_LITE_EVENT_NODE_JOIN:
            Serial.println("[MESH] Node joined");
            break;
        case ESP_MESH_LITE_EVENT_NODE_LEAVE:
            Serial.println("[MESH] Node left");
            break;
        case ESP_MESH_LITE_EVENT_NODE_CHANGE:
            Serial.println("[MESH] Node changed");
            break;
        default:
            break;
    }
}

void Gateway::onMqttMessage(const char* topic, const char* payload)
{
    Serial.printf("[MQTT] Rx: %s = %s\n", topic, payload);

    if (strstr(topic, "/broadcast")) {
        meshHandler.broadcast(payload);
        Serial.println("[MESH] Broadcast sent");
    } else if (strstr(topic, "/cmd")) {
        String targetId = extractTargetId(topic);
        if (targetId.length() > 0) {
            meshHandler.sendToNode(targetId.c_str(), payload);
            Serial.printf("[MESH] Unicast to %s\n", targetId.c_str());
        }
    }
}

String Gateway::extractTargetId(const char* topic)
{
    String t = String(topic);
    int start = t.indexOf('/') + 1;
    int end = t.indexOf('/', start);
    if (end > start) {
        return t.substring(start, end);
    }
    return "";
}
