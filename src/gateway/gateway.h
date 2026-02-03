#pragma once

#include <Arduino.h>
#include <esp_wifi.h>
#include <cJSON.h>
#include "../config.h"
#include "../mesh/mesh_handler.h"
#include "../mesh/binary_protocol.h"
#include "../mqtt/mqtt_client.h"
#if OTA_ENABLED
#include "../ota/ota_handler.h"
extern "C" {
#include "core/esp_mesh_lite_core.h"
}
#include <esp_ota_ops.h>
#endif

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
    bool _ntpInitialized;

    // MQTT publishing
    void publishStatus(const char* deviceId, uint8_t level, bool isRoot,
                       uint32_t heap, int8_t rssi, const char* parentId,
                       uint8_t phy, uint32_t timestamp, const char* version);
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

#if OTA_ENABLED
    // OTA
    static void onOtaProgress(uint8_t percent, size_t written, size_t total);
    static void onOtaComplete(bool success, OtaError error, const char* message);
    static esp_err_t lanOtaProvideFile(esp_mesh_lite_lan_ota_file_transfer_param_t *param);
    static esp_err_t lanOtaGetFile(esp_mesh_lite_lan_ota_file_transfer_param_t *param);
    static esp_err_t lanOtaGetFileDone(void);
    void publishOtaStatus();
    void publishOtaStatusPayload(const char* deviceId, cJSON* json);
    void handleOtaCommand(const char* topic, const char* payload);
    void startLanOta();
    void reportLanOtaProgress(size_t bytesWritten, size_t bytesTotal);
    void reportLanOtaState(const char* state, const char* errorMessage = nullptr);
    void scheduleRestart(const char* reason);
    void handleLanOtaFinish();
#endif

    static Gateway* _instance;

#if OTA_ENABLED
    bool _lanOtaPending;
    bool _lanOtaInProgress;
    bool _lanOtaRestartPending;
    uint8_t _lanOtaLastPercent;
    unsigned long _lanOtaLastReportMs;
    size_t _lanOtaSize;
    char _lanOtaVersion[32];
    char _otaJobId[64];
    const esp_partition_t* _lanOtaSourcePartition;
    esp_ota_handle_t _lanOtaHandle;
    const esp_partition_t* _lanOtaTargetPartition;
#endif
};

extern Gateway gateway;
