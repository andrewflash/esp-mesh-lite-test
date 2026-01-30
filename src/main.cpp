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

// Create mesh instance
MeshLite mesh;

// Timer for periodic status updates
unsigned long lastStatusTime = 0;
const unsigned long STATUS_INTERVAL = 5000; // 5 seconds

// Mesh event callback function
void meshEventCallback(esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    Serial.printf("[MESH] Event ID: %ld\n", event_id);

    switch (event_id) {
        case ESP_MESH_LITE_EVENT_CORE_STARTED:
            Serial.println("[MESH] Core started");
            Serial.printf("  - Level: %d\n", mesh.getLevel());
            Serial.printf("  - Mesh ID: %d\n", mesh.getMeshId());
            Serial.printf("  - Is Root: %s\n", mesh.isRoot() ? "Yes" : "No");
            Serial.printf("  - Is Leaf: %s\n", mesh.isLeafNode() ? "Yes" : "No");
            break;

        case ESP_MESH_LITE_EVENT_CORE_INHERITED_NET_SEGMENT_CHANGED:
            Serial.println("[MESH] Network segment changed");
            Serial.printf("  - New Level: %d\n", mesh.getLevel());
            break;

        case ESP_MESH_LITE_EVENT_CORE_ROUTER_INFO_CHANGED:
            Serial.println("[MESH] Router info changed");
            break;

        case ESP_MESH_LITE_EVENT_NODE_JOIN:
            Serial.println("[MESH] Node joined");
            Serial.printf("  - Total nodes: %lu\n", mesh.getNodeCount());
            break;

        case ESP_MESH_LITE_EVENT_NODE_LEAVE:
            Serial.println("[MESH] Node left");
            Serial.printf("  - Total nodes: %lu\n", mesh.getNodeCount());
            break;

        case ESP_MESH_LITE_EVENT_NODE_CHANGE:
            Serial.println("[MESH] Node changed");
            Serial.printf("  - Total nodes: %lu\n", mesh.getNodeCount());
            break;

        default:
            break;
    }
}

// Message handler for custom "status_request" messages
cJSON* handleStatusRequest(cJSON* payload, uint32_t seq)
{
    Serial.printf("[MSG] Received status request, seq=%lu\n", seq);

    // Create response
    cJSON* response = cJSON_CreateObject();
    if (response) {
        cJSON_AddNumberToObject(response, "level", mesh.getLevel());
        cJSON_AddNumberToObject(response, "heap", ESP.getFreeHeap());
        cJSON_AddStringToObject(response, "category", mesh.getDeviceCategory());
        cJSON_AddBoolToObject(response, "is_root", mesh.isRoot());
    }

    return response;
}

void setup()
{
    // Initialize serial communication
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n========================================");
    Serial.println("  ESP-Mesh-Lite Arduino Example");
    Serial.println("  (Stub Implementation)");
    Serial.println("========================================\n");

    // Set event callback (before begin)
    mesh.onEvent(meshEventCallback);

    // Configure credentials from .env
    Serial.printf("[SETUP] Router: %s\n", WIFI_SSID);
    mesh.setRouterCredentials(WIFI_SSID, WIFI_PASSWORD);

    Serial.printf("[SETUP] SoftAP: %s\n", MESH_SOFTAP_SSID);
    mesh.setSoftAPCredentials(MESH_SOFTAP_SSID, MESH_SOFTAP_PASSWORD);

    mesh.setMeshId(MESH_ID);

    // Initialize mesh
    Serial.println("[SETUP] Initializing mesh...");
    if (!mesh.begin()) {
        Serial.println("[ERROR] Failed to initialize mesh!");
        while (1) {
            delay(1000);
        }
    }

    // Register message handler for custom messages
    mesh.onMessage("status_request", "status_response", handleStatusRequest);

    // Start the mesh network
    Serial.println("[SETUP] Starting mesh network...");
    mesh.start();

    Serial.println("[SETUP] Mesh network started!");
}

void loop()
{
    unsigned long currentTime = millis();

    // Status update - adding back calls one at a time to find crash
    if (currentTime - lastStatusTime >= STATUS_INTERVAL) {
        lastStatusTime = currentTime;

        Serial.println("--- Mesh Status ---");
        Serial.printf("  Level: %d\n", mesh.getLevel());
        Serial.printf("  Mesh ID: %d\n", mesh.getMeshId());
        Serial.printf("  Is Root: %s\n", mesh.isRoot() ? "Yes" : "No");
        Serial.printf("  Is Leaf: %s\n", mesh.isLeafNode() ? "Yes" : "No");
        // Serial.printf("  Node Count: %lu\n", mesh.getNodeCount());  // Test this
        // Serial.printf("  Root IP: %s\n", rootIP);  // Test this
        Serial.printf("  Heap: %lu\n", ESP.getFreeHeap());
        Serial.println("-------------------\n");
    }

    delay(10);
}
