/*
 * ESP-Mesh-Lite with MQTT Gateway
 *
 * Architecture:
 *   [Child Nodes] --mesh--> [Root/Gateway] --MQTT--> [Broker]
 *   [Broker] --MQTT--> [Root/Gateway] --mesh--> [Child Nodes]
 *
 * MQTT Topics (device-centric structure):
 *   mesh-lite/{device_id}/status  - node status (all nodes, unified format)
 *   mesh-lite/{device_id}/data    - data from node
 *   mesh-lite/{device_id}/cmd     - command to node (downlink)
 *   mesh-lite/broadcast           - broadcast to all nodes
 *
 * Status payload: {"id":"...","level":N,"root":bool,"heap":N,"rssi":N}
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "config.h"
#include "mesh/mesh_handler.h"
#include "mesh/binary_protocol.h"
#include "mqtt/mqtt_client.h"

// Get WiFi RSSI using ESP-IDF API (works with mesh-lite)
int8_t getWiFiRSSI()
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

// Set to true to use binary protocol for status messages
#define USE_BINARY_STATUS true

unsigned long lastPublishTime = 0;
unsigned long lastLevelLogTime = 0;
bool mqttSubscribed = false;

// Publish unified status to MQTT (used by root for itself and forwarding child status)
void publishStatus(const char* deviceId, uint8_t level, bool isRoot, uint32_t heap, int8_t rssi)
{
    if (!mqtt.isConnected()) return;

    char topic[64];
    char payload[160];

    // Device-centric topic: mesh-lite/{device_id}/status
    snprintf(topic, sizeof(topic), "%s/%s/status", MQTT_TOPIC_PREFIX, deviceId);

    // Unified payload structure
    snprintf(payload, sizeof(payload),
             "{\"id\":\"%s\",\"level\":%d,\"root\":%s,\"heap\":%lu,\"rssi\":%d}",
             deviceId, level, isRoot ? "true" : "false", heap, rssi);

    if (mqtt.publish(topic, payload)) {
        Serial.printf("[MQTT] Status: %s\n", topic);
    }
}

// Publish data to MQTT (used by root for itself and forwarding child data)
void publishData(const char* deviceId, const char* data)
{
    if (!mqtt.isConnected()) return;

    char topic[64];
    char payload[256];

    // Device-centric topic: mesh-lite/{device_id}/data
    snprintf(topic, sizeof(topic), "%s/%s/data", MQTT_TOPIC_PREFIX, deviceId);
    snprintf(payload, sizeof(payload), "{\"id\":\"%s\",\"data\":%s}", deviceId, data);

    if (mqtt.publish(topic, payload)) {
        Serial.printf("[MQTT] Data: %s\n", topic);
    }
}

// Called when node receives mesh message (JSON)
void onMeshMessage(const char* from, const char* data)
{
    Serial.printf("[MESH] From %s: %s\n", from, data);

    // If root, forward child messages to MQTT using unified format
    if (meshHandler.isRoot()) {
        publishData(from, data);
    }
}

// Called when node receives binary mesh message
void onBinaryMessage(const uint8_t* data, size_t len)
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

    // Handle different message types
    switch (header.type) {
        case MSG_TYPE_STATUS: {
            if (len >= sizeof(StatusMsg)) {
                const StatusMsg* status = (const StatusMsg*)data;
                Serial.printf("[MESH-BIN] Status: level=%d, heap=%lu, rssi=%d\n",
                              status->level, status->heap, status->rssi);

                // If root, forward to MQTT using unified format
                if (meshHandler.isRoot()) {
                    publishStatus(macStr, status->level, false, status->heap, status->rssi);
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

void onMeshEvent(int32_t eventId)
{
    switch (eventId) {
        case ESP_MESH_LITE_EVENT_CORE_STARTED:
            Serial.println("[MESH] Started");
            break;
        case ESP_MESH_LITE_EVENT_CORE_INHERITED_NET_SEGMENT_CHANGED:
            Serial.printf("[MESH] Level: %d (%s)\n",
                meshHandler.getLevel(),
                meshHandler.isRoot() ? "Root" : "Node");
            mqttSubscribed = false;
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

// Extract device ID from topic: mesh-lite/{device_id}/cmd
String extractTargetId(const char* topic)
{
    String t = String(topic);
    int start = t.indexOf('/') + 1;
    int end = t.indexOf('/', start);
    if (end > start) {
        return t.substring(start, end);
    }
    return "";
}

// Called when root receives MQTT message
void onMqttMessage(const char* topic, const char* payload)
{
    Serial.printf("[MQTT] Rx: %s = %s\n", topic, payload);

    if (strstr(topic, "/broadcast")) {
        // Broadcast to all nodes
        meshHandler.broadcast(payload);
        Serial.println("[MESH] Broadcast sent");
    } else if (strstr(topic, "/cmd")) {
        // Unicast to specific node
        String targetId = extractTargetId(topic);
        if (targetId.length() > 0) {
            meshHandler.sendToNode(targetId.c_str(), payload);
            Serial.printf("[MESH] Unicast to %s\n", targetId.c_str());
        }
    }
}

void publishRootStatus()
{
    // Use unified status format
    publishStatus(
        meshHandler.getDeviceId().c_str(),
        meshHandler.getLevel(),
        true,  // isRoot
        ESP.getFreeHeap(),
        getWiFiRSSI()
    );
}

void sendStatusToRoot()
{
#if USE_BINARY_STATUS
    // Use compact binary protocol (16 bytes vs ~30 bytes JSON)
    if (meshHandler.sendStatusToRoot()) {
        Serial.printf("[MESH-BIN] Tx status to root (16 bytes)\n");
    }
#else
    // Use JSON protocol
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

void subscribeToCommands()
{
    if (mqttSubscribed) return;

    char topic[64];

    // Subscribe to unicast commands for all nodes (root forwards)
    snprintf(topic, sizeof(topic), "%s/+/cmd", MQTT_TOPIC_PREFIX);
    mqtt.subscribe(topic);

    // Subscribe to broadcast
    snprintf(topic, sizeof(topic), "%s/broadcast", MQTT_TOPIC_PREFIX);
    mqtt.subscribe(topic);

    mqttSubscribed = true;
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n========================================");
    Serial.println("   ESP-Mesh-Lite + MQTT Gateway");
    Serial.println("========================================\n");

    meshHandler.onEvent(onMeshEvent);
    meshHandler.onMessage(onMeshMessage);
    meshHandler.onBinaryMessage(onBinaryMessage);

    Serial.printf("[SETUP] Router: %s, Mesh ID: %d\n", WIFI_SSID, MESH_ID);

    if (!meshHandler.begin(WIFI_SSID, WIFI_PASSWORD,
                           MESH_SOFTAP_SSID, MESH_SOFTAP_PASSWORD,
                           MESH_ID)) {
        Serial.println("[ERROR] Mesh init failed!");
        while (1) delay(1000);
    }

    meshHandler.start();
    Serial.printf("[SETUP] Device: %s\n", meshHandler.getDeviceId().c_str());

    mqtt.begin(MQTT_BROKER, MQTT_PORT, meshHandler.getDeviceId().c_str());
    mqtt.setCredentials(MQTT_USER, MQTT_PASSWORD);
    mqtt.setCallback(onMqttMessage);

    Serial.printf("[SETUP] MQTT: %s:%d\n", MQTT_BROKER, MQTT_PORT);
    Serial.println("[SETUP] Ready\n");

    lastPublishTime = millis();
}

void loop()
{
    // Log level every 5 seconds for debugging
    if (millis() - lastLevelLogTime >= 5000) {
        lastLevelLogTime = millis();
        Serial.printf("[DEBUG] Level: %d, Root: %s, Connected: %s\n",
            meshHandler.getLevel(),
            meshHandler.isRoot() ? "yes" : "no",
            meshHandler.isConnected() ? "yes" : "no");
    }

    if (meshHandler.isRoot()) {
        if (!mqtt.isConnected()) {
            mqtt.connect();
        } else {
            mqtt.loop();
            subscribeToCommands();

            if (millis() - lastPublishTime >= STATUS_INTERVAL_MS) {
                lastPublishTime = millis();
                publishRootStatus();
            }
        }
    } else if (meshHandler.isConnected()) {
        if (millis() - lastPublishTime >= STATUS_INTERVAL_MS) {
            lastPublishTime = millis();
            sendStatusToRoot();
        }
    }

    delay(100);
}
