#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Binary Protocol for ESP-Mesh-Lite
 *
 * Optimized binary format for mesh communication.
 * Reduces payload size significantly compared to JSON.
 *
 * Example: Status message
 *   JSON:   {"level":2,"heap":123456} = ~30 bytes
 *   Binary: 16 bytes (header + level + heap + rssi)
 */

// Message types
enum BinaryMsgType : uint8_t {
    MSG_TYPE_STATUS     = 0x01,  // Node status report
    MSG_TYPE_COMMAND    = 0x02,  // Command to node
    MSG_TYPE_DATA       = 0x03,  // Generic data payload
    MSG_TYPE_ACK        = 0x10,  // Acknowledgment
    MSG_TYPE_NACK       = 0x11,  // Negative acknowledgment
};

// Command subtypes for MSG_TYPE_COMMAND
enum CommandType : uint8_t {
    CMD_REBOOT          = 0x01,
    CMD_CONFIG_UPDATE   = 0x02,
    CMD_LED_CONTROL     = 0x03,
    CMD_PING            = 0x04,
    CMD_REQUEST_STATUS  = 0x05,
};

// Ensure structures are packed (no padding)
#pragma pack(push, 1)

// Header for all binary messages
struct BinaryMsgHeader {
    uint8_t type;       // BinaryMsgType
    uint8_t mac[6];     // Source MAC address
    uint16_t seq;       // Sequence number for tracking
};

// Status message (child -> root)
struct StatusMsg {
    BinaryMsgHeader header;
    uint8_t level;      // Mesh level
    uint32_t heap;      // Free heap in bytes
    int8_t rssi;        // WiFi RSSI
    uint8_t parentMac[6]; // Parent MAC (router BSSID if root, mesh parent STA MAC if child)
    uint8_t phy;        // Negotiated WiFi PHY mode (wifi_phy_mode_t: 1=LR,2=11B,3=11G,4=HT20,5=HT40,6=HE20)
};
// Total: 9 + 1 + 4 + 1 + 6 + 1 = 22 bytes

// Command message (root -> child)
struct CommandMsg {
    BinaryMsgHeader header;
    uint8_t cmdType;    // CommandType
    uint8_t dataLen;    // Length of optional data
    uint8_t data[32];   // Optional command data
};

// Data message (generic payload)
struct DataMsg {
    BinaryMsgHeader header;
    uint16_t dataLen;   // Length of data
    uint8_t data[128];  // Payload data
};

// Acknowledgment message
struct AckMsg {
    BinaryMsgHeader header;
    uint16_t ackSeq;    // Sequence being acknowledged
    uint8_t status;     // 0 = success, >0 = error code
};

#pragma pack(pop)

// Binary protocol helper class
class BinaryProtocol {
public:
    // Sequence counter for messages
    static uint16_t nextSeq();

    // Fill header with MAC and sequence
    static void fillHeader(BinaryMsgHeader* header, uint8_t type, const uint8_t* mac);

    // Create status message
    static size_t createStatusMsg(uint8_t* buffer, size_t bufLen,
                                   const uint8_t* mac, uint8_t level,
                                   uint32_t heap, int8_t rssi,
                                   const uint8_t* parentMac, uint8_t phy);

    // Create command message
    static size_t createCommandMsg(uint8_t* buffer, size_t bufLen,
                                    const uint8_t* mac, uint8_t cmdType,
                                    const uint8_t* data = nullptr, uint8_t dataLen = 0);

    // Create data message
    static size_t createDataMsg(uint8_t* buffer, size_t bufLen,
                                 const uint8_t* mac,
                                 const uint8_t* data, uint16_t dataLen);

    // Parse header from buffer
    static bool parseHeader(const uint8_t* buffer, size_t len, BinaryMsgHeader* header);

    // Format MAC address as string
    static void macToString(const uint8_t* mac, char* str, size_t len);

    // Parse MAC from string (12 hex chars)
    static bool stringToMac(const char* str, uint8_t* mac);
};
