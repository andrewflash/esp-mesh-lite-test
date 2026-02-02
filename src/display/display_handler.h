#pragma once

#include <Arduino.h>
#include "../config.h"

#if DISPLAY_ENABLED

#include <U8g2lib.h>
#include <Wire.h>

class DisplayHandler {
public:
    DisplayHandler();

    bool begin();
    void clear();
    void update();

    // Status display
    void showStatus(const char* deviceId, uint8_t level, bool isRoot,
                    bool mqttConnected, int8_t rssi, uint32_t heap,
                    const char* parentId);
    void showMessage(const char* line1, const char* line2 = nullptr,
                     const char* line3 = nullptr);
    void showProgress(const char* title, uint8_t percent);

    // Low-level access
    U8G2* getDisplay() { return _display; }

private:
    U8G2* _display;
    bool _initialized;

    void createDisplay();
    void drawHeader(const char* title);
    void drawFooter(const char* text);
};

extern DisplayHandler display;

#else

// Stub class when display is disabled
class DisplayHandler {
public:
    DisplayHandler() {}
    bool begin() { return false; }
    void clear() {}
    void update() {}
    void showStatus(const char*, uint8_t, bool, bool, int8_t, uint32_t, const char*) {}
    void showMessage(const char*, const char* = nullptr, const char* = nullptr) {}
    void showProgress(const char*, uint8_t) {}
};

extern DisplayHandler display;

#endif
