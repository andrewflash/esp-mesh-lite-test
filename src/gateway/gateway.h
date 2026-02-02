#pragma once

#include <Arduino.h>
#include <esp_wifi.h>
#include "../config.h"
#include "../mesh/mesh_handler.h"
#include "../mesh/binary_protocol.h"
#include "../mqtt/mqtt_client.h"

// Set to true to use binary protocol for child status messages
#ifndef USE_BINARY_STATUS
#define USE_BINARY_STATUS true
#endif

class Gateway {
public:
    Gateway();

    void begin();
    void loop();

    // Get WiFi RSSI
    static int8_t getWiFiRSSI();

    // Check if WiFi STA has valid IP (ready for network operations)
    static bool isWiFiReady();

private:
    unsigned long _lastPublishTime;
    unsigned long _lastMqttAttempt;
    bool _mqttSubscribed;

    // MQTT publishing
    void publishStatus(const char* deviceId, uint8_t level, bool isRoot,
                       uint32_t heap, int8_t rssi, const char* parentId, uint8_t phy);
    void publishData(const char* deviceId, const char* data);
    void publishRootStatus();

    // Mesh sending
    void sendStatusToRoot();

    // MQTT subscriptions
    void subscribeToCommands();

    // Callbacks
    static void onMeshMessage(const char* from, const char* data);
    static void onBinaryMessage(const uint8_t* data, size_t len);
    static void onMeshEvent(int32_t eventId);
    static void onMqttMessage(const char* topic, const char* payload);

    // Helpers
    static String extractTargetId(const char* topic);

    static Gateway* _instance;
};

extern Gateway gateway;
