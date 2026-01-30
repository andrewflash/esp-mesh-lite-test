/*
 * ESP-Mesh-Lite Arduino Example
 * Set credentials in .env file (copy from .env.example)
 */

#include <Arduino.h>
#include <ESP_Mesh_Lite.h>

// Default values if not defined in build flags
#ifndef WIFI_SSID
#define WIFI_SSID "DefaultSSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "DefaultPassword"
#endif
#ifndef MESH_SOFTAP_SSID
#define MESH_SOFTAP_SSID "ESP"
#endif
#ifndef MESH_SOFTAP_PASSWORD
#define MESH_SOFTAP_PASSWORD "12345678"
#endif
#ifndef MESH_ID
#define MESH_ID 77
#endif

MeshLite mesh;
unsigned long lastStatusTime = 0;
const unsigned long STATUS_INTERVAL = 5000;

void printStatus()
{
    char rootIP[16] = "N/A";
    mesh.getRootIP(rootIP, sizeof(rootIP));

    Serial.println("┌─────────────────────────────┐");
    Serial.println("│       Mesh Status           │");
    Serial.println("├─────────────────────────────┤");
    Serial.printf("│ Level:      %d               │\n", mesh.getLevel());
    Serial.printf("│ Mesh ID:    %d              │\n", mesh.getMeshId());
    Serial.printf("│ Role:       %-14s  │\n", mesh.isRoot() ? "Root" : (mesh.isLeafNode() ? "Leaf" : "Node"));
    Serial.printf("│ Nodes:      %-14lu  │\n", mesh.getNodeCount());
    Serial.printf("│ Root IP:    %-14s  │\n", rootIP);
    Serial.printf("│ Free Heap:  %-10lu bytes│\n", ESP.getFreeHeap());
    Serial.println("└─────────────────────────────┘");
}

void meshEventCallback(esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch (event_id) {
        case ESP_MESH_LITE_EVENT_CORE_STARTED:
            Serial.println("[MESH] Core started");
            break;
        case ESP_MESH_LITE_EVENT_CORE_INHERITED_NET_SEGMENT_CHANGED:
            Serial.printf("[MESH] Level changed: %d\n", mesh.getLevel());
            break;
        case ESP_MESH_LITE_EVENT_CORE_ROUTER_INFO_CHANGED:
            Serial.println("[MESH] Router info updated");
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

cJSON* handleStatusRequest(cJSON* payload, uint32_t seq)
{
    cJSON* response = cJSON_CreateObject();
    if (response) {
        cJSON_AddNumberToObject(response, "level", mesh.getLevel());
        cJSON_AddNumberToObject(response, "heap", ESP.getFreeHeap());
        cJSON_AddNumberToObject(response, "nodes", mesh.getNodeCount());
        cJSON_AddStringToObject(response, "category", mesh.getDeviceCategory());
        cJSON_AddBoolToObject(response, "is_root", mesh.isRoot());
    }
    return response;
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n========================================");
    Serial.println("       ESP-Mesh-Lite Example");
    Serial.println("========================================\n");

    mesh.onEvent(meshEventCallback);

    Serial.printf("[SETUP] Router SSID: %s\n", WIFI_SSID);
    Serial.printf("[SETUP] Mesh ID: %d\n", MESH_ID);

    mesh.setRouterCredentials(WIFI_SSID, WIFI_PASSWORD);
    mesh.setSoftAPCredentials(MESH_SOFTAP_SSID, MESH_SOFTAP_PASSWORD);
    mesh.setMeshId(MESH_ID);

    if (!mesh.begin()) {
        Serial.println("[ERROR] Mesh init failed!");
        while (1) delay(1000);
    }

    mesh.onMessage("status_request", "status_response", handleStatusRequest);
    mesh.start();

    Serial.println("[SETUP] Mesh started\n");
}

void loop()
{
    if (millis() - lastStatusTime >= STATUS_INTERVAL) {
        lastStatusTime = millis();
        printStatus();
    }
    delay(10);
}
