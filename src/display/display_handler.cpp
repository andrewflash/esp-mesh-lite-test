#include "display_handler.h"

#if DISPLAY_ENABLED

DisplayHandler display;

DisplayHandler::DisplayHandler()
    : _display(nullptr)
    , _initialized(false)
{
}

void DisplayHandler::createDisplay()
{
    // Create display based on configuration
    // Using software I2C for flexibility with pin assignment

#if DISPLAY_TYPE == DISPLAY_SSD1306
    #if DISPLAY_HEIGHT == 64
        _display = new U8G2_SSD1306_128X64_NONAME_F_SW_I2C(
            U8G2_R0 + DISPLAY_ROTATION,
            DISPLAY_SCL_PIN,
            DISPLAY_SDA_PIN,
            U8X8_PIN_NONE
        );
    #else // 32 pixel height
        _display = new U8G2_SSD1306_128X32_UNIVISION_F_SW_I2C(
            U8G2_R0 + DISPLAY_ROTATION,
            DISPLAY_SCL_PIN,
            DISPLAY_SDA_PIN,
            U8X8_PIN_NONE
        );
    #endif
#elif DISPLAY_TYPE == DISPLAY_SH1106
    _display = new U8G2_SH1106_128X64_NONAME_F_SW_I2C(
        U8G2_R0 + DISPLAY_ROTATION,
        DISPLAY_SCL_PIN,
        DISPLAY_SDA_PIN,
        U8X8_PIN_NONE
    );
#else
    #error "Unknown DISPLAY_TYPE"
#endif
}

bool DisplayHandler::begin()
{
    if (_initialized) return true;

    createDisplay();

    if (!_display) {
        Serial.println("[DISPLAY] Failed to create display");
        return false;
    }

    _display->setI2CAddress(DISPLAY_I2C_ADDR * 2);  // U8g2 uses 8-bit address
    _display->begin();
    _display->setFont(u8g2_font_6x10_tf);
    _display->setFontRefHeightExtendedText();
    _display->setDrawColor(1);
    _display->setFontPosTop();

    _initialized = true;
    Serial.printf("[DISPLAY] Initialized: %dx%d, I2C=0x%02X, SDA=%d, SCL=%d\n",
                  DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_I2C_ADDR,
                  DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);

    // Show boot message
    showMessage("ESP-Mesh-Lite", "Starting...");

    return true;
}

void DisplayHandler::clear()
{
    if (!_initialized) return;
    _display->clearBuffer();
}

void DisplayHandler::update()
{
    if (!_initialized) return;
    _display->sendBuffer();
}

void DisplayHandler::drawHeader(const char* title)
{
    _display->setFont(u8g2_font_6x10_tf);
    _display->drawStr(0, 0, title);
    _display->drawHLine(0, 11, DISPLAY_WIDTH);
}

void DisplayHandler::drawFooter(const char* text)
{
    int y = DISPLAY_HEIGHT - 10;
    _display->drawHLine(0, y - 2, DISPLAY_WIDTH);
    _display->setFont(u8g2_font_5x7_tf);
    _display->drawStr(0, y, text);
}

void DisplayHandler::showStatus(const char* deviceId, uint8_t level, bool isRoot,
                                 bool mqttConnected, int8_t rssi, uint32_t heap,
                                 const char* parentId)
{
    if (!_initialized) return;

    clear();

    // Header
    drawHeader("Mesh-Lite Gateway");

    // Status content - use smaller line height to fit parent info
    _display->setFont(u8g2_font_6x10_tf);
    int y = 14;
    int lineHeight = 10;

    // Device ID (truncate if needed)
    char line[32];
    snprintf(line, sizeof(line), "ID: %.12s", deviceId);
    _display->drawStr(0, y, line);
    y += lineHeight;

    // Level and role + MQTT status
    snprintf(line, sizeof(line), "L%d %s", level, isRoot ? "[ROOT]" : "[NODE]");
    _display->drawStr(0, y, line);
    _display->drawStr(72, y, mqttConnected ? "MQTT:OK" : "MQTT:--");
    y += lineHeight;

    // Parent ID (show last 6 chars for brevity)
    if (parentId && strlen(parentId) > 0) {
        const char* shortParent = strlen(parentId) > 6 ? parentId + 6 : parentId;
        snprintf(line, sizeof(line), "P: ...%s", shortParent);
    } else {
        snprintf(line, sizeof(line), "P: --");
    }
    _display->drawStr(0, y, line);

    // Heap on same line
    snprintf(line, sizeof(line), "%luK", heap / 1024);
    _display->drawStr(90, y, line);
    y += lineHeight;

    // RSSI
    snprintf(line, sizeof(line), "RSSI: %ddBm", rssi);
    _display->drawStr(0, y, line);

    update();
}

void DisplayHandler::showMessage(const char* line1, const char* line2, const char* line3)
{
    if (!_initialized) return;

    clear();

    _display->setFont(u8g2_font_6x10_tf);

    int y = (DISPLAY_HEIGHT - 30) / 2;  // Center vertically

    if (line1) {
        int x = (DISPLAY_WIDTH - _display->getStrWidth(line1)) / 2;
        _display->drawStr(x, y, line1);
        y += 12;
    }

    if (line2) {
        _display->setFont(u8g2_font_5x7_tf);
        int x = (DISPLAY_WIDTH - _display->getStrWidth(line2)) / 2;
        _display->drawStr(x, y, line2);
        y += 10;
    }

    if (line3) {
        int x = (DISPLAY_WIDTH - _display->getStrWidth(line3)) / 2;
        _display->drawStr(x, y, line3);
    }

    update();
}

void DisplayHandler::showProgress(const char* title, uint8_t percent)
{
    if (!_initialized) return;

    clear();

    _display->setFont(u8g2_font_6x10_tf);

    // Title centered
    int x = (DISPLAY_WIDTH - _display->getStrWidth(title)) / 2;
    _display->drawStr(x, 20, title);

    // Progress bar
    int barWidth = DISPLAY_WIDTH - 20;
    int barHeight = 10;
    int barX = 10;
    int barY = 38;

    _display->drawFrame(barX, barY, barWidth, barHeight);
    int fillWidth = (barWidth - 2) * percent / 100;
    _display->drawBox(barX + 1, barY + 1, fillWidth, barHeight - 2);

    // Percentage text
    char pctText[8];
    snprintf(pctText, sizeof(pctText), "%d%%", percent);
    x = (DISPLAY_WIDTH - _display->getStrWidth(pctText)) / 2;
    _display->drawStr(x, 52, pctText);

    update();
}

#else

// Stub implementation when display is disabled
DisplayHandler display;

#endif
