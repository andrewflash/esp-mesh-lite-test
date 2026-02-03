#include "gateway.h"
#include <esp_netif.h>
#include <cJSON.h>
#include <string.h>

Gateway* Gateway::_instance = nullptr;
Gateway gateway;

Gateway::Gateway()
    : _lastPublishTime(0)
    , _lastMqttAttempt(0)
    , _mqttSubscribed(false)
    , _ntpInitialized(false)
#if OTA_ENABLED
    , _lanOtaPending(false)
    , _lanOtaInProgress(false)
    , _lanOtaRestartPending(false)
    , _lanOtaLastPercent(0)
    , _lanOtaLastReportMs(0)
    , _lanOtaSize(0)
    , _lanOtaSourcePartition(nullptr)
    , _lanOtaHandle(0)
    , _lanOtaTargetPartition(nullptr)
#endif
{
    _instance = this;
#if OTA_ENABLED
    _lanOtaVersion[0] = '\0';
    _otaJobId[0] = '\0';
#endif
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

#if OTA_ENABLED
    // Register OTA callbacks
    otaHandler.onProgress(onOtaProgress);
    otaHandler.onComplete(onOtaComplete);

    static esp_mesh_lite_lan_ota_file_transfer_cb_t lanOtaCb = {
        .provide_file_cb = lanOtaProvideFile,
        .get_file_cb = lanOtaGetFile,
        .get_file_done = lanOtaGetFileDone,
    };
    esp_mesh_lite_ota_register_file_transfer_cb(&lanOtaCb);
#endif

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
                            uint8_t phy, uint32_t timestamp, const char* version)
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
    if (version && version[0]) {
        cJSON_AddStringToObject(json, "version", version);
    }

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
        timestamp,
        PROJECT_VERSION
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
    cJSON_AddNumberToObject(json, "rssi", meshHandler.getWiFiRSSI());
    cJSON_AddStringToObject(json, "version", PROJECT_VERSION);

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

#if OTA_ENABLED
    // Subscribe to OTA commands for this device
    snprintf(topic, sizeof(topic), "%s/%s/ota/cmd", MQTT_TOPIC_PREFIX,
             meshHandler.getDeviceId().c_str());
    mqtt.subscribe(topic);

    // Subscribe to broadcast OTA commands
    snprintf(topic, sizeof(topic), "%s/broadcast/ota", MQTT_TOPIC_PREFIX);
    mqtt.subscribe(topic);
#endif

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
        bool isOta = type && cJSON_IsString(type) && strcmp(type->valuestring, "ota") == 0;
        bool hasFields = level && heap && rssi;

        if (isStatus || hasFields) {
            cJSON* ts = cJSON_GetObjectItem(json, "ts");
            cJSON* version = cJSON_GetObjectItem(json, "version");
            const char* versionStr = (version && cJSON_IsString(version)) ? version->valuestring : nullptr;
            _instance->publishStatus(from,
                                     level ? (uint8_t)level->valueint : 0,
                                     false,
                                     heap ? (uint32_t)heap->valuedouble : 0,
                                     rssi ? (int8_t)rssi->valueint : 0,
                                     parent && cJSON_IsString(parent) ? parent->valuestring : "",
                                     phy ? (uint8_t)phy->valueint : 0,
                                     ts ? (uint32_t)ts->valuedouble : 0,
                                     versionStr);
            cJSON_Delete(json);
            return;
        }

        if (isOta) {
            cJSON_DeleteItemFromObject(json, "type");
            _instance->publishOtaStatusPayload(from, json);
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
                    const char* version = nullptr;
                    if (status->version[0] != '\0') {
                        version = status->version;
                    }
                    _instance->publishStatus(macStr, status->level, false,
                                             status->heap, status->rssi, parentStr, status->phy,
                                             status->timestamp, version);
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
#if OTA_ENABLED
        case ESP_MESH_LITE_EVENT_OTA_START:
            Serial.println("[MESH-OTA] LAN OTA started");
            break;
        case ESP_MESH_LITE_EVENT_OTA_PROGRESS:
            Serial.println("[MESH-OTA] LAN OTA progress");
            break;
        case ESP_MESH_LITE_EVENT_OTA_FINISH:
            Serial.println("[MESH-OTA] LAN OTA finished");
            if (_instance && _instance->_lanOtaInProgress && meshHandler.isRoot()) {
                _instance->handleLanOtaFinish();
            }
            break;
#endif
        default:
            break;
    }
}

void Gateway::onMqttMessage(const char* topic, const char* payload)
{
    Serial.printf("[MQTT] Rx: %s = %s\n", topic, payload);

#if OTA_ENABLED
    // Handle OTA commands (check before other handlers)
    if (strstr(topic, "/ota/cmd") || strstr(topic, "/broadcast/ota")) {
        if (_instance) {
            _instance->handleOtaCommand(topic, payload);
        }
        return;
    }
#endif

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

#if OTA_ENABLED
void Gateway::handleOtaCommand(const char* topic, const char* payload)
{
    cJSON* json = cJSON_Parse(payload);
    if (!json) {
        Serial.println("[OTA] Invalid JSON command");
        return;
    }

    cJSON* action = cJSON_GetObjectItem(json, "action");
    if (!action || !cJSON_IsString(action)) {
        Serial.println("[OTA] Missing 'action' field");
        cJSON_Delete(json);
        return;
    }

    const char* actionStr = action->valuestring;

    if (strcmp(actionStr, "start") == 0) {
        // Start OTA update
        cJSON* url = cJSON_GetObjectItem(json, "url");
        if (!url || !cJSON_IsString(url)) {
            Serial.println("[OTA] Missing 'url' field");
            cJSON_Delete(json);
            return;
        }
        cJSON* jobIdItem = cJSON_GetObjectItem(json, "job_id");
        if (jobIdItem && cJSON_IsString(jobIdItem)) {
            strncpy(_otaJobId, jobIdItem->valuestring, sizeof(_otaJobId) - 1);
            _otaJobId[sizeof(_otaJobId) - 1] = '\0';
        } else {
            _otaJobId[0] = '\0';
        }

        const char* version = nullptr;
        const char* sha256 = nullptr;
        size_t size = 0;

        cJSON* versionItem = cJSON_GetObjectItem(json, "version");
        if (versionItem && cJSON_IsString(versionItem)) {
            version = versionItem->valuestring;
        }

        cJSON* sha256Item = cJSON_GetObjectItem(json, "sha256");
        if (sha256Item && cJSON_IsString(sha256Item)) {
            sha256 = sha256Item->valuestring;
        }

        cJSON* sizeItem = cJSON_GetObjectItem(json, "size");
        if (sizeItem && cJSON_IsNumber(sizeItem)) {
            size = (size_t)sizeItem->valuedouble;
        }

        bool lanOtaRequested = false;
        cJSON* meshFlag = cJSON_GetObjectItem(json, "mesh");
        if (meshFlag && cJSON_IsBool(meshFlag)) {
            lanOtaRequested = cJSON_IsTrue(meshFlag);
        }
        if (topic && strstr(topic, "/broadcast/ota")) {
            lanOtaRequested = true;
        }

        if (lanOtaRequested && meshHandler.isRoot()) {
            _lanOtaPending = true;
            _lanOtaRestartPending = true;
            _lanOtaSize = size;
            if (version && version[0]) {
                strncpy(_lanOtaVersion, version, sizeof(_lanOtaVersion) - 1);
                _lanOtaVersion[sizeof(_lanOtaVersion) - 1] = '\0';
            } else {
                _lanOtaVersion[0] = '\0';
            }
            otaHandler.setAutoRestart(false);
        } else {
            _lanOtaPending = false;
            _lanOtaRestartPending = false;
            otaHandler.setAutoRestart(true);
        }

        if (otaHandler.startUpdate(url->valuestring, version, sha256, size)) {
            publishOtaStatus();
        } else {
            _lanOtaPending = false;
            _lanOtaRestartPending = false;
            publishOtaStatus();  // Publish error status
        }
    }
    else if (strcmp(actionStr, "cancel") == 0) {
        otaHandler.cancel();
        _otaJobId[0] = '\0';
        publishOtaStatus();
    }
    else if (strcmp(actionStr, "status") == 0) {
        publishOtaStatus();
    }
    else {
        Serial.printf("[OTA] Unknown action: %s\n", actionStr);
    }

    cJSON_Delete(json);
}

void Gateway::publishOtaStatus()
{
    if (!mqtt.isConnected()) return;

    cJSON* json = cJSON_CreateObject();
    if (!json) return;

    cJSON_AddStringToObject(json, "state", OtaHandler::stateToString(otaHandler.getState()));
    cJSON_AddNumberToObject(json, "progress", otaHandler.getProgress());
    if (_otaJobId[0] != '\0') {
        cJSON_AddStringToObject(json, "job_id", _otaJobId);
    }

    if (otaHandler.getBytesTotal() > 0) {
        cJSON_AddNumberToObject(json, "bytes_written", otaHandler.getBytesWritten());
        cJSON_AddNumberToObject(json, "bytes_total", otaHandler.getBytesTotal());
    }

    const char* version = otaHandler.getTargetVersion();
    if (version && version[0]) {
        cJSON_AddStringToObject(json, "version", version);
    }

    OtaError error = otaHandler.getLastError();
    if (error != OtaError::NONE) {
        cJSON_AddStringToObject(json, "error", OtaHandler::errorToString(error));
        const char* errMsg = otaHandler.getLastErrorMessage();
        if (errMsg && errMsg[0]) {
            cJSON_AddStringToObject(json, "error_message", errMsg);
        }
    }

    char* payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!payload) return;

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/%s/ota/status", MQTT_TOPIC_PREFIX,
             meshHandler.getDeviceId().c_str());

    if (mqtt.publish(topic, payload)) {
        Serial.printf("[OTA] Status: %s\n", payload);
    }

    cJSON_free(payload);
}

void Gateway::publishOtaStatusPayload(const char* deviceId, cJSON* json)
{
    if (!json) return;
    if (!mqtt.isConnected()) {
        cJSON_Delete(json);
        return;
    }

    char* payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!payload) return;

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/%s/ota/status", MQTT_TOPIC_PREFIX, deviceId);

    if (mqtt.publish(topic, payload)) {
        Serial.printf("[OTA] Status: %s\n", payload);
    }

    cJSON_free(payload);
}

void Gateway::onOtaProgress(uint8_t percent, size_t written, size_t total)
{
    if (_instance) {
        _instance->publishOtaStatus();
    }
}

void Gateway::onOtaComplete(bool success, OtaError error, const char* message)
{
    if (!_instance) {
        return;
    }
    _instance->publishOtaStatus();

    if (success && _instance->_lanOtaPending && meshHandler.isRoot()) {
        _instance->_lanOtaPending = false;
        _instance->startLanOta();
        return;
    }

    if (success) {
        Serial.println("[OTA] Update complete");
    } else {
        Serial.printf("[OTA] Update failed: %s\n", message);
        _instance->_lanOtaPending = false;
        _instance->_lanOtaRestartPending = false;
    }
}

esp_err_t Gateway::lanOtaProvideFile(esp_mesh_lite_lan_ota_file_transfer_param_t *param)
{
    if (!_instance || !param || !_instance->_lanOtaSourcePartition) {
        return ESP_FAIL;
    }

    size_t totalSize = _instance->_lanOtaSize;
    if (totalSize == 0) {
        totalSize = _instance->_lanOtaSourcePartition->size;
    }

    size_t offset = param->offset;
    size_t readSize = param->data_size;
    if (offset >= totalSize) {
        memset(param->data, 0xFF, readSize);
        return ESP_OK;
    }

    size_t available = totalSize - offset;
    size_t toRead = available < readSize ? available : readSize;
    esp_err_t err = esp_partition_read(_instance->_lanOtaSourcePartition, offset, param->data, toRead);
    if (err != ESP_OK) {
        return err;
    }
    if (toRead < readSize) {
        memset(param->data + toRead, 0xFF, readSize - toRead);
    }
    return ESP_OK;
}

esp_err_t Gateway::lanOtaGetFile(esp_mesh_lite_lan_ota_file_transfer_param_t *param)
{
    if (!_instance || !param) {
        return ESP_FAIL;
    }

    if (param->offset == 0 && _instance->_lanOtaHandle != 0) {
        esp_ota_end(_instance->_lanOtaHandle);
        _instance->_lanOtaHandle = 0;
        _instance->_lanOtaTargetPartition = nullptr;
    }
    if (param->offset == 0 && !meshHandler.isRoot()) {
        _instance->_lanOtaLastPercent = 0;
        _instance->_lanOtaLastReportMs = 0;
    }

    if (_instance->_lanOtaHandle == 0) {
        _instance->_lanOtaTargetPartition = esp_ota_get_next_update_partition(nullptr);
        if (!_instance->_lanOtaTargetPartition) {
            return ESP_FAIL;
        }
        esp_err_t err = esp_ota_begin(_instance->_lanOtaTargetPartition, OTA_WITH_SEQUENTIAL_WRITES, &_instance->_lanOtaHandle);
        if (err != ESP_OK) {
            return err;
        }
    }

    esp_err_t err = esp_ota_write(_instance->_lanOtaHandle, param->data, param->data_size);
    if (err == ESP_OK && !meshHandler.isRoot()) {
        size_t total = param->filesize > 0 ? static_cast<size_t>(param->filesize) : _instance->_lanOtaSize;
        size_t written = param->offset + param->data_size;
        _instance->reportLanOtaProgress(written, total);
    }
    return err;
}

esp_err_t Gateway::lanOtaGetFileDone(void)
{
    if (!_instance) {
        return ESP_FAIL;
    }
    if (_instance->_lanOtaHandle == 0 || !_instance->_lanOtaTargetPartition) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_ota_end(_instance->_lanOtaHandle);
    _instance->_lanOtaHandle = 0;

    if (err != ESP_OK) {
        _instance->_lanOtaTargetPartition = nullptr;
        if (!meshHandler.isRoot()) {
            _instance->reportLanOtaState("failed", "ota_end_failed");
        }
        return err;
    }

    err = esp_ota_set_boot_partition(_instance->_lanOtaTargetPartition);
    _instance->_lanOtaTargetPartition = nullptr;
    if (err != ESP_OK) {
        if (!meshHandler.isRoot()) {
            _instance->reportLanOtaState("failed", "set_boot_failed");
        }
        return err;
    }

    if (!meshHandler.isRoot()) {
        _instance->reportLanOtaState("complete");
        Serial.println("[MESH-OTA] Node update ready, rebooting in 3 seconds...");
        delay(3000);
        esp_restart();
    }

    return ESP_OK;
}

void Gateway::startLanOta()
{
    if (!meshHandler.isRoot()) {
        return;
    }

    _lanOtaSourcePartition = otaHandler.getUpdatePartition();
    if (!_lanOtaSourcePartition) {
        _lanOtaSourcePartition = esp_ota_get_running_partition();
    }
    if (!_lanOtaSourcePartition) {
        Serial.println("[MESH-OTA] No source partition available");
        scheduleRestart("missing partition");
        return;
    }

    if (_lanOtaSize == 0) {
        _lanOtaSize = _lanOtaSourcePartition->size;
    }
    if (_lanOtaVersion[0] == '\0') {
        const char* version = otaHandler.getTargetVersion();
        if (version && version[0]) {
            strncpy(_lanOtaVersion, version, sizeof(_lanOtaVersion) - 1);
            _lanOtaVersion[sizeof(_lanOtaVersion) - 1] = '\0';
        }
    }

    esp_err_t err = esp_mesh_lite_wait_ota_allow();
    if (err != ESP_OK) {
        Serial.printf("[MESH-OTA] OTA wait failed: %d\n", err);
        scheduleRestart("ota wait failed");
        return;
    }

    esp_mesh_lite_file_transmit_config_t transmitConfig = {};
    transmitConfig.type = ESP_MESH_LITE_OTA_TRANSMIT_FIRMWARE;
    strncpy(transmitConfig.fw_version, _lanOtaVersion, sizeof(transmitConfig.fw_version) - 1);
    transmitConfig.fw_version[sizeof(transmitConfig.fw_version) - 1] = '\0';
    transmitConfig.size = _lanOtaSize;
    transmitConfig.extern_url_ota_cb = nullptr;

    err = esp_mesh_lite_transmit_file_start(&transmitConfig);
    if (err != ESP_OK) {
        Serial.printf("[MESH-OTA] LAN OTA start failed: %d\n", err);
        scheduleRestart("lan ota start failed");
        return;
    }

    _lanOtaInProgress = true;
    reportLanOtaState("lan_ota_started");
    Serial.printf("[MESH-OTA] LAN OTA started (size=%u)\n", static_cast<unsigned>(_lanOtaSize));
}

void Gateway::handleLanOtaFinish()
{
    _lanOtaInProgress = false;
    Serial.println("[MESH-OTA] LAN OTA complete");
    reportLanOtaState("lan_ota_finished");
    scheduleRestart("lan ota finished");
}

void Gateway::reportLanOtaProgress(size_t bytesWritten, size_t bytesTotal)
{
    if (meshHandler.isRoot()) {
        return;
    }
    if (bytesTotal == 0) {
        return;
    }
    uint8_t percent = static_cast<uint8_t>((bytesWritten * 100) / bytesTotal);
    unsigned long now = millis();
    if (percent == _lanOtaLastPercent && (now - _lanOtaLastReportMs) < 2000) {
        return;
    }
    if (percent < _lanOtaLastPercent) {
        return;
    }
    _lanOtaLastPercent = percent;
    _lanOtaLastReportMs = now;

    cJSON* json = cJSON_CreateObject();
    if (!json) return;
    cJSON_AddStringToObject(json, "type", "ota");
    cJSON_AddStringToObject(json, "state", "downloading");
    cJSON_AddNumberToObject(json, "progress", percent);
    cJSON_AddNumberToObject(json, "bytes_written", static_cast<double>(bytesWritten));
    cJSON_AddNumberToObject(json, "bytes_total", static_cast<double>(bytesTotal));
    if (_lanOtaVersion[0] != '\0') {
        cJSON_AddStringToObject(json, "version", _lanOtaVersion);
    }
    if (_otaJobId[0] != '\0') {
        cJSON_AddStringToObject(json, "job_id", _otaJobId);
    }

    char* payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!payload) return;
    meshHandler.sendToRoot(payload);
    cJSON_free(payload);
}

void Gateway::reportLanOtaState(const char* state, const char* errorMessage)
{
    if (meshHandler.isRoot()) {
        cJSON* json = cJSON_CreateObject();
        if (!json) return;
        cJSON_AddStringToObject(json, "state", state);
        if (_lanOtaVersion[0] != '\0') {
            cJSON_AddStringToObject(json, "version", _lanOtaVersion);
        }
        if (errorMessage && errorMessage[0]) {
            cJSON_AddStringToObject(json, "error_message", errorMessage);
        }
        publishOtaStatusPayload(meshHandler.getDeviceId().c_str(), json);
        return;
    }

    cJSON* json = cJSON_CreateObject();
    if (!json) return;
    cJSON_AddStringToObject(json, "type", "ota");
    cJSON_AddStringToObject(json, "state", state);
    if (_lanOtaVersion[0] != '\0') {
        cJSON_AddStringToObject(json, "version", _lanOtaVersion);
    }
    if (_otaJobId[0] != '\0') {
        cJSON_AddStringToObject(json, "job_id", _otaJobId);
    }
    if (errorMessage && errorMessage[0]) {
        cJSON_AddStringToObject(json, "error_message", errorMessage);
    }
    char* payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!payload) return;
    meshHandler.sendToRoot(payload);
    cJSON_free(payload);
}

void Gateway::scheduleRestart(const char* reason)
{
    if (!_lanOtaRestartPending) {
        return;
    }
    Serial.printf("[OTA] Restarting in 5 seconds (%s)\n", reason ? reason : "done");
    delay(5000);
    esp_restart();
}
#endif // OTA_ENABLED
