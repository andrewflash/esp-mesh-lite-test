#include "binary_protocol.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

uint16_t BinaryProtocol::nextSeq()
{
    static uint16_t seq = 0;
    return seq++;
}

void BinaryProtocol::fillHeader(BinaryMsgHeader* header, uint8_t type, const uint8_t* mac)
{
    header->type = type;
    memcpy(header->mac, mac, 6);
    header->seq = nextSeq();
}

size_t BinaryProtocol::createStatusMsg(uint8_t* buffer, size_t bufLen,
                                        const uint8_t* mac, uint8_t level,
                                        uint32_t heap, int8_t rssi,
                                        const uint8_t* parentMac, uint8_t phy,
                                        uint32_t timestamp, const char* version)
{
    if (bufLen < sizeof(StatusMsg)) return 0;

    StatusMsg* msg = (StatusMsg*)buffer;
    fillHeader(&msg->header, MSG_TYPE_STATUS, mac);
    msg->level = level;
    msg->heap = heap;
    msg->rssi = rssi;
    memcpy(msg->parentMac, parentMac, 6);
    msg->phy = phy;
    msg->timestamp = timestamp;
    memset(msg->version, 0, sizeof(msg->version));
    if (version && version[0]) {
        strncpy(msg->version, version, sizeof(msg->version) - 1);
        msg->version[sizeof(msg->version) - 1] = '\0';
    }

    return sizeof(StatusMsg);
}

size_t BinaryProtocol::createCommandMsg(uint8_t* buffer, size_t bufLen,
                                         const uint8_t* mac, uint8_t cmdType,
                                         const uint8_t* data, uint8_t dataLen)
{
    if (bufLen < sizeof(CommandMsg)) return 0;
    if (dataLen > 32) dataLen = 32;

    CommandMsg* msg = (CommandMsg*)buffer;
    fillHeader(&msg->header, MSG_TYPE_COMMAND, mac);
    msg->cmdType = cmdType;
    msg->dataLen = dataLen;
    if (data && dataLen > 0) {
        memcpy(msg->data, data, dataLen);
    }

    return sizeof(BinaryMsgHeader) + 2 + dataLen;
}

size_t BinaryProtocol::createDataMsg(uint8_t* buffer, size_t bufLen,
                                      const uint8_t* mac,
                                      const uint8_t* data, uint16_t dataLen)
{
    if (bufLen < sizeof(BinaryMsgHeader) + 2 + dataLen) return 0;
    if (dataLen > 128) dataLen = 128;

    DataMsg* msg = (DataMsg*)buffer;
    fillHeader(&msg->header, MSG_TYPE_DATA, mac);
    msg->dataLen = dataLen;
    memcpy(msg->data, data, dataLen);

    return sizeof(BinaryMsgHeader) + 2 + dataLen;
}

bool BinaryProtocol::parseHeader(const uint8_t* buffer, size_t len, BinaryMsgHeader* header)
{
    if (len < sizeof(BinaryMsgHeader)) return false;
    memcpy(header, buffer, sizeof(BinaryMsgHeader));
    return true;
}

void BinaryProtocol::macToString(const uint8_t* mac, char* str, size_t len)
{
    if (len < 13) return;
    snprintf(str, len, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool BinaryProtocol::stringToMac(const char* str, uint8_t* mac)
{
    if (strlen(str) < 12) return false;
    for (int i = 0; i < 6; i++) {
        char hex[3] = { str[i*2], str[i*2+1], 0 };
        mac[i] = (uint8_t)strtol(hex, nullptr, 16);
    }
    return true;
}
