#pragma once

#include <Arduino.h>
#include <ESP_Mesh_Lite.h>
#include "binary_protocol.h"

typedef void (*MeshEventCallback)(int32_t eventId);
typedef void (*MeshMessageCallback)(const char* from, const char* data);
typedef void (*MeshBinaryCallback)(const uint8_t* data, size_t len);

class MeshHandler {
public:
    MeshHandler();

    bool begin(const char* routerSsid, const char* routerPassword,
               const char* softapSsid, const char* softapPassword,
               uint8_t meshId);
    void start();

    uint8_t getLevel();
    bool isRoot();
    bool isConnected();
    String getDeviceId();

    // Uplink: child -> root (JSON)
    bool sendToRoot(const char* data);

    // Downlink: root -> children (JSON)
    bool sendToNode(const char* targetId, const char* data);  // Unicast (logical)
    bool broadcast(const char* data);                          // Broadcast

    // Binary protocol methods
    bool sendBinaryToRoot(const uint8_t* data, size_t len);
    bool sendBinaryToChildren(const uint8_t* data, size_t len);
    bool sendStatusToRoot();  // Convenience: sends binary status message

    // Get MAC address for binary protocol
    const uint8_t* getMac() const { return _mac; }

    void onMessage(MeshMessageCallback callback);
    void onBinaryMessage(MeshBinaryCallback callback);
    void onEvent(MeshEventCallback callback);

    // Connection failure tracking (call from app when network operations fail)
    void reportNetworkFailure();    // Call when DNS/MQTT fails while root
    void reportNetworkSuccess();    // Call when network operation succeeds
    uint8_t getConnectionFailures() const { return _connectionFailures; }
    bool isMeshOnlyMode() const { return _meshOnlyMode; }

private:
    MeshLite _mesh;
    String _deviceId;
    uint8_t _mac[6];
    MeshEventCallback _eventCallback;
    MeshMessageCallback _messageCallback;
    MeshBinaryCallback _binaryCallback;

    // Connection failure tracking
    uint8_t _connectionFailures;
    bool _meshOnlyMode;

    static void meshEventHandler(esp_event_base_t base, int32_t id, void* data);
    static cJSON* meshMessageHandler(cJSON* payload, uint32_t seq);
    static cJSON* meshBinaryHandler(cJSON* payload, uint32_t seq);
    static MeshHandler* _instance;

    void generateDeviceId();
    void switchToMeshOnlyMode();
};

extern MeshHandler meshHandler;
