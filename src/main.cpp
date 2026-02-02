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
 * Status payload: {"id":"...","level":N,"root":bool,"heap":N,"rssi":N,"parent":"...","phy":"..."}
 */

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include "config.h"
#include "mesh/mesh_handler.h"
#include "mqtt/mqtt_client.h"
#include "display/display_handler.h"
#include "gateway/gateway.h"

// Timing
static unsigned long lastDebugLog = 0;
static unsigned long lastDisplayUpdate = 0;

void logDebugStatus()
{
    if (millis() - lastDebugLog < 5000) return;
    lastDebugLog = millis();

    // Log what we're actually connected to
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        Serial.printf("[DEBUG] Connected to SSID: %s, BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
            ap_info.ssid,
            ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2],
            ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5]);
    }

    if (meshHandler.isMeshOnlyMode()) {
        Serial.printf("[DEBUG] Level: %d, Root: %s, Connected: %s, Mode: mesh-only\n",
            meshHandler.getLevel(),
            meshHandler.isRoot() ? "yes" : "no",
            meshHandler.isConnected() ? "yes" : "no");
    } else if (meshHandler.getConnectionFailures() > 0) {
        Serial.printf("[DEBUG] Level: %d, Root: %s, Connected: %s, NetFails: %d/%d\n",
            meshHandler.getLevel(),
            meshHandler.isRoot() ? "yes" : "no",
            meshHandler.isConnected() ? "yes" : "no",
            meshHandler.getConnectionFailures(),
            MESH_MAX_ROUTER_FAILURES);
    } else {
        Serial.printf("[DEBUG] Level: %d, Root: %s, Connected: %s\n",
            meshHandler.getLevel(),
            meshHandler.isRoot() ? "yes" : "no",
            meshHandler.isConnected() ? "yes" : "no");
    }
}

void updateDisplay()
{
    if (millis() - lastDisplayUpdate < DISPLAY_UPDATE_MS) return;
    lastDisplayUpdate = millis();

    display.showStatus(
        meshHandler.getDeviceId().c_str(),
        meshHandler.getLevel(),
        meshHandler.isRoot(),
        mqtt.isConnected(),
        Gateway::getWiFiRSSI(),
        ESP.getFreeHeap(),
        meshHandler.getParentId().c_str()
    );
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n========================================");
    Serial.println("   ESP-Mesh-Lite + MQTT Gateway");
    Serial.printf("   Version: %s\n", PROJECT_VERSION);
    Serial.println("========================================\n");

#if ERASE_NVS_ON_BOOT
    // Erase NVS to clear stored WiFi credentials (development only)
    Serial.println("[SETUP] Erasing NVS...");
    nvs_flash_erase();
    nvs_flash_init();
#endif

    // Initialize watchdog timer
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&wdt_config);
    esp_task_wdt_add(NULL);
    Serial.printf("[SETUP] Watchdog: %ds timeout\n", WDT_TIMEOUT_SEC);

    // Initialize display
    if (display.begin()) {
        Serial.println("[SETUP] Display initialized");
    }

    // Initialize mesh
    Serial.printf("[SETUP] Router: %s, Mesh ID: %d\n", WIFI_SSID, MESH_ID);

    if (!meshHandler.begin(WIFI_SSID, WIFI_PASSWORD,
                           MESH_SOFTAP_SSID, MESH_SOFTAP_PASSWORD,
                           MESH_ID)) {
        Serial.println("[ERROR] Mesh init failed! Watchdog will restart...");
        display.showMessage("ERROR", "Mesh init failed", "Restarting...");
        // Watchdog will trigger restart - no need for infinite loop
        while (1) delay(1000);
    }

    meshHandler.start();
    Serial.printf("[SETUP] Device: %s\n", meshHandler.getDeviceId().c_str());

    display.showMessage("Mesh Started", meshHandler.getDeviceId().c_str(), "Connecting...");

    // Initialize MQTT (client ID = "{prefix}-{MAC}" for uniqueness)
    static String mqttClientId = String(MQTT_CLIENT_PREFIX) + "-" + meshHandler.getDeviceId();
    mqtt.begin(MQTT_BROKER, MQTT_PORT, mqttClientId.c_str());
    mqtt.setCredentials(MQTT_USER, MQTT_PASSWORD);
    Serial.printf("[SETUP] MQTT: %s:%d, client=%s\n", MQTT_BROKER, MQTT_PORT, mqttClientId.c_str());

    // Initialize gateway (mesh-MQTT bridge)
    gateway.begin();

    Serial.println("[SETUP] Ready\n");
}

void loop()
{
    // Feed watchdog
    esp_task_wdt_reset();

    logDebugStatus();
    updateDisplay();
    gateway.loop();

    delay(100);
}
