#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

typedef void (*MqttMessageCallback)(const char* topic, const char* payload);

class MqttClient {
public:
    MqttClient();

    void begin(const char* broker, uint16_t port, const char* clientId);
    void setCredentials(const char* user, const char* password);
    void setCallback(MqttMessageCallback callback);

    bool connect();
    void disconnect();
    bool isConnected();
    void loop();

    bool subscribe(const char* topic);
    bool publish(const char* topic, const char* payload, bool retained = false);

private:
    WiFiClient _wifiClient;
    PubSubClient _mqttClient;
    MqttMessageCallback _callback;

    const char* _broker;
    uint16_t _port;
    const char* _clientId;
    const char* _user;
    const char* _password;

    unsigned long _lastReconnectAttempt;

    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    static MqttClient* _instance;
};

extern MqttClient mqtt;
