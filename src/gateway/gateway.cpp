#include "gateway.h"

Gateway* Gateway::_instance = nullptr;
Gateway gateway;

Gateway::Gateway()
    : _lastPublishTime(0)
    , _lastMqttAttempt(0)
    , _mqttSubscribed(false)
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
    } else if (meshHandler.isConnected()) {
        if (millis() - _lastPublishTime >= STATUS_INTERVAL_MS) {
            _lastPublishTime = millis();
            sendStatusToRoot();
        }
    }
}

void Gateway::publishStatus(const char* deviceId, uint8_t level, bool isRoot,
                            uint32_t heap, int8_t rssi)
{
    if (!mqtt.isConnected()) return;

    char topic[64];
    char payload[160];

    snprintf(topic, sizeof(topic), "%s/%s/status", MQTT_TOPIC_PREFIX, deviceId);
    snprintf(payload, sizeof(payload),
             "{\"id\":\"%s\",\"level\":%d,\"root\":%s,\"heap\":%lu,\"rssi\":%d}",
             deviceId, level, isRoot ? "true" : "false", heap, rssi);

    if (mqtt.publish(topic, payload)) {
        Serial.printf("[MQTT] Status: %s\n", topic);
    }
}

void Gateway::publishData(const char* deviceId, const char* data)
{
    if (!mqtt.isConnected()) return;

    char topic[64];
    char payload[256];

    snprintf(topic, sizeof(topic), "%s/%s/data", MQTT_TOPIC_PREFIX, deviceId);
    snprintf(payload, sizeof(payload), "{\"id\":\"%s\",\"data\":%s}", deviceId, data);

    if (mqtt.publish(topic, payload)) {
        Serial.printf("[MQTT] Data: %s\n", topic);
    }
}

void Gateway::publishRootStatus()
{
    publishStatus(
        meshHandler.getDeviceId().c_str(),
        meshHandler.getLevel(),
        true,
        ESP.getFreeHeap(),
        getWiFiRSSI()
    );
}

void Gateway::sendStatusToRoot()
{
#if USE_BINARY_STATUS
    if (meshHandler.sendStatusToRoot()) {
        Serial.printf("[MESH-BIN] Tx status to root (16 bytes)\n");
    }
#else
    char payload[96];
    snprintf(payload, sizeof(payload),
             "{\"level\":%d,\"heap\":%lu}",
             meshHandler.getLevel(),
             ESP.getFreeHeap());

    if (meshHandler.sendToRoot(payload)) {
        Serial.printf("[MESH] Tx to root: %s\n", payload);
    }
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

    if (meshHandler.isRoot() && _instance) {
        _instance->publishData(from, data);
    }
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
                Serial.printf("[MESH-BIN] Status: level=%d, heap=%lu, rssi=%d\n",
                              status->level, status->heap, status->rssi);

                if (meshHandler.isRoot() && _instance) {
                    _instance->publishStatus(macStr, status->level, false,
                                             status->heap, status->rssi);
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
            break;
        case ESP_MESH_LITE_EVENT_NODE_JOIN:
            Serial.println("[MESH] Node joined");
            break;
        case ESP_MESH_LITE_EVENT_NODE_LEAVE:
            Serial.println("[MESH] Node left");
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
