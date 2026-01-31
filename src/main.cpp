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
#include <esp_task_wdt.h>
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
        ESP.getFreeHeap()
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

    // Initialize MQTT
    mqtt.begin(MQTT_BROKER, MQTT_PORT, meshHandler.getDeviceId().c_str());
    mqtt.setCredentials(MQTT_USER, MQTT_PASSWORD);
    Serial.printf("[SETUP] MQTT: %s:%d\n", MQTT_BROKER, MQTT_PORT);

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
