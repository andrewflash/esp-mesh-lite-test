#pragma once

#include <Arduino.h>
#include "../config.h"

#if OTA_ENABLED
#include <esp_ota_ops.h>

// OTA States
enum class OtaState : uint8_t {
    IDLE = 0,
    STARTING,
    DOWNLOADING,
    WRITING,
    VERIFYING,
    COMPLETE,
    FAILED,
    CANCELLED
};

// OTA Error Codes
enum class OtaError : uint8_t {
    NONE = 0,
    INVALID_URL,
    CONNECTION_FAILED,
    HTTP_ERROR,
    TIMEOUT,
    CHECKSUM_MISMATCH,
    SIGNATURE_INVALID,
    WRITE_FAILED,
    PARTITION_ERROR,
    VERSION_INVALID,
    CANCELLED,
    UNKNOWN
};

// Progress callback: (percent, bytesWritten, bytesTotal)
typedef void (*OtaProgressCallback)(uint8_t percent, size_t written, size_t total);

// Complete callback: (success, error, errorMessage)
typedef void (*OtaCompleteCallback)(bool success, OtaError error, const char* message);

class OtaHandler {
public:
    OtaHandler();

    // Start OTA update from URL
    // url: Full URL to firmware binary
    // version: Expected version (optional, for logging)
    // sha256: Expected SHA256 hash (optional, 64 hex chars)
    // size: Expected file size in bytes (optional, 0 = unknown)
    bool startUpdate(const char* url, const char* version = nullptr,
                     const char* sha256 = nullptr, size_t size = 0);

    // Cancel ongoing update
    void cancel();

    // Check current state
    OtaState getState() const { return _state; }
    bool isUpdating() const { return _state != OtaState::IDLE && _state != OtaState::COMPLETE && _state != OtaState::FAILED; }
    bool isIdle() const { return _state == OtaState::IDLE; }

    // Progress info
    uint8_t getProgress() const { return _progress; }
    size_t getBytesWritten() const { return _bytesWritten; }
    size_t getBytesTotal() const { return _bytesTotal; }
    OtaError getLastError() const { return _lastError; }
    const char* getLastErrorMessage() const { return _lastErrorMessage; }

    // Version info
    const char* getTargetVersion() const { return _targetVersion; }

    // Callbacks
    void onProgress(OtaProgressCallback callback) { _progressCallback = callback; }
    void onComplete(OtaCompleteCallback callback) { _completeCallback = callback; }
    void setAutoRestart(bool enabled) { _autoRestart = enabled; }
    const esp_partition_t* getUpdatePartition() const { return _updatePartition; }

    // Convert state/error to string (for JSON status)
    static const char* stateToString(OtaState state);
    static const char* errorToString(OtaError error);

    // Singleton access
    static OtaHandler& instance();

private:
    OtaState _state;
    OtaError _lastError;
    char _lastErrorMessage[64];
    char _targetVersion[16];
    char _expectedSha256[65];
    size_t _expectedSize;

    uint8_t _progress;
    size_t _bytesWritten;
    size_t _bytesTotal;

    bool _cancelRequested;
    unsigned long _startTime;
    bool _autoRestart;
    const esp_partition_t* _updatePartition;

    OtaProgressCallback _progressCallback;
    OtaCompleteCallback _completeCallback;

    // Internal methods
    void setState(OtaState state);
    void setError(OtaError error, const char* message = nullptr);
    void reportProgress(uint8_t percent, size_t written, size_t total);
    void reportComplete(bool success);

    // OTA task (runs in separate FreeRTOS task)
    static void otaTask(void* param);
    void performOta(const char* url);

    // Verification
    bool verifyChecksum(const char* expected);

    static OtaHandler* _instance;
};

extern OtaHandler otaHandler;

#endif // OTA_ENABLED
