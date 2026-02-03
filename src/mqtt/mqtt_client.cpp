#include "mqtt_client.h"
#include "../config.h"

MqttClient* MqttClient::_instance = nullptr;
MqttClient mqtt;

MqttClient::MqttClient()
    : _mqttClient(_wifiClient)
    , _callback(nullptr)
    , _broker(nullptr)
    , _port(1883)
    , _clientId(nullptr)
    , _user(nullptr)
    , _password(nullptr)
    , _lastReconnectAttempt(0)
{
    _instance = this;
}

void MqttClient::begin(const char* broker, uint16_t port, const char* clientId)
{
    _broker = broker;
    _port = port;
    _clientId = clientId;
    _mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
    _mqttClient.setServer(_broker, _port);
    _mqttClient.setCallback(mqttCallback);
}

void MqttClient::setCredentials(const char* user, const char* password)
{
    _user = user;
    _password = password;
}

void MqttClient::setCallback(MqttMessageCallback callback)
{
    _callback = callback;
}

void MqttClient::mqttCallback(char* topic, byte* payload, unsigned int length)
{
    if (_instance && _instance->_callback) {
        char message[length + 1];
        memcpy(message, payload, length);
        message[length] = '\0';
        _instance->_callback(topic, message);
    }
}

bool MqttClient::connect()
{
    if (_mqttClient.connected()) {
        return true;
    }

    unsigned long now = millis();
    if (now - _lastReconnectAttempt < MQTT_RECONNECT_INTERVAL_MS) {
        return false;
    }
    _lastReconnectAttempt = now;

    Serial.printf("[MQTT] Connecting to %s:%d...\n", _broker, _port);

    bool connected;
    if (_user && strlen(_user) > 0) {
        connected = _mqttClient.connect(_clientId, _user, _password);
    } else {
        connected = _mqttClient.connect(_clientId);
    }

    if (connected) {
        Serial.println("[MQTT] Connected");
        return true;
    }

    Serial.printf("[MQTT] Failed, rc=%d\n", _mqttClient.state());
    return false;
}

void MqttClient::disconnect()
{
    _mqttClient.disconnect();
}

bool MqttClient::isConnected()
{
    return _mqttClient.connected();
}

void MqttClient::loop()
{
    _mqttClient.loop();
}

bool MqttClient::subscribe(const char* topic)
{
    if (!_mqttClient.connected()) {
        return false;
    }
    bool result = _mqttClient.subscribe(topic);
    if (result) {
        Serial.printf("[MQTT] Subscribed: %s\n", topic);
    }
    return result;
}

bool MqttClient::publish(const char* topic, const char* payload, bool retained)
{
    if (!_mqttClient.connected()) {
        return false;
    }
    return _mqttClient.publish(topic, payload, retained);
}
