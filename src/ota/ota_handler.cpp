#include "ota_handler.h"

#if OTA_ENABLED

#include <esp_ota_ops.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_crt_bundle.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

OtaHandler* OtaHandler::_instance = nullptr;
OtaHandler otaHandler;

// URL storage for OTA task
static char _otaUrl[256];

OtaHandler::OtaHandler()
    : _state(OtaState::IDLE)
    , _lastError(OtaError::NONE)
    , _expectedSize(0)
    , _progress(0)
    , _bytesWritten(0)
    , _bytesTotal(0)
    , _cancelRequested(false)
    , _startTime(0)
    , _autoRestart(true)
    , _updatePartition(nullptr)
    , _progressCallback(nullptr)
    , _completeCallback(nullptr)
{
    _lastErrorMessage[0] = '\0';
    _targetVersion[0] = '\0';
    _expectedSha256[0] = '\0';
    _instance = this;
}

OtaHandler& OtaHandler::instance()
{
    return *_instance;
}

bool OtaHandler::startUpdate(const char* url, const char* version,
                              const char* sha256, size_t size)
{
    if (!url || strlen(url) == 0) {
        setError(OtaError::INVALID_URL, "URL is empty");
        return false;
    }

    if (isUpdating()) {
        setError(OtaError::UNKNOWN, "Update already in progress");
        return false;
    }

    // Store parameters
    strncpy(_otaUrl, url, sizeof(_otaUrl) - 1);
    _otaUrl[sizeof(_otaUrl) - 1] = '\0';

    if (version) {
        strncpy(_targetVersion, version, sizeof(_targetVersion) - 1);
        _targetVersion[sizeof(_targetVersion) - 1] = '\0';
    } else {
        _targetVersion[0] = '\0';
    }

    if (sha256 && strlen(sha256) == 64) {
        strncpy(_expectedSha256, sha256, sizeof(_expectedSha256) - 1);
        _expectedSha256[sizeof(_expectedSha256) - 1] = '\0';
    } else {
        _expectedSha256[0] = '\0';
    }

    _expectedSize = size;
    _cancelRequested = false;
    _progress = 0;
    _bytesWritten = 0;
    _bytesTotal = size;
    _lastError = OtaError::NONE;
    _lastErrorMessage[0] = '\0';
    _startTime = millis();

    setState(OtaState::STARTING);

    _updatePartition = esp_ota_get_next_update_partition(nullptr);

    // Create OTA task with sufficient stack
    BaseType_t result = xTaskCreate(
        otaTask,
        "ota_task",
        8192,
        this,
        5,
        nullptr
    );

    if (result != pdPASS) {
        setError(OtaError::UNKNOWN, "Failed to create OTA task");
        setState(OtaState::FAILED);
        return false;
    }

    Serial.printf("[OTA] Starting update from: %s\n", url);
    if (_targetVersion[0]) {
        Serial.printf("[OTA] Target version: %s\n", _targetVersion);
    }

    return true;
}

void OtaHandler::cancel()
{
    if (isUpdating()) {
        _cancelRequested = true;
        Serial.println("[OTA] Cancel requested");
    }
}

void OtaHandler::setState(OtaState state)
{
    _state = state;
    Serial.printf("[OTA] State: %s\n", stateToString(state));
}

void OtaHandler::setError(OtaError error, const char* message)
{
    _lastError = error;
    if (message) {
        strncpy(_lastErrorMessage, message, sizeof(_lastErrorMessage) - 1);
        _lastErrorMessage[sizeof(_lastErrorMessage) - 1] = '\0';
    } else {
        strncpy(_lastErrorMessage, errorToString(error), sizeof(_lastErrorMessage) - 1);
    }
    Serial.printf("[OTA] Error: %s - %s\n", errorToString(error), _lastErrorMessage);
}

void OtaHandler::reportProgress(uint8_t percent, size_t written, size_t total)
{
    _progress = percent;
    _bytesWritten = written;
    _bytesTotal = total;

    if (_progressCallback) {
        _progressCallback(percent, written, total);
    }
}

void OtaHandler::reportComplete(bool success)
{
    if (_completeCallback) {
        _completeCallback(success, _lastError, _lastErrorMessage);
    }
}

void OtaHandler::otaTask(void* param)
{
    OtaHandler* handler = (OtaHandler*)param;
    handler->performOta(_otaUrl);
    vTaskDelete(nullptr);
}

void OtaHandler::performOta(const char* url)
{
    setState(OtaState::DOWNLOADING);

#if OTA_USE_HTTPS
    // Configure HTTP client
    esp_http_client_config_t httpConfig = {};
    httpConfig.url = url;
    httpConfig.timeout_ms = OTA_TIMEOUT_SEC * 1000;
    httpConfig.keep_alive_enable = true;
    httpConfig.transport_type = HTTP_TRANSPORT_OVER_SSL;
    // Use ESP's certificate bundle for HTTPS
    httpConfig.crt_bundle_attach = esp_crt_bundle_attach;

    // Configure OTA
    esp_https_ota_config_t otaConfig = {};
    otaConfig.http_config = &httpConfig;

    esp_https_ota_handle_t otaHandle = nullptr;
    esp_err_t err = esp_https_ota_begin(&otaConfig, &otaHandle);

    if (err != ESP_OK) {
        if (err == ESP_ERR_HTTP_CONNECT) {
            setError(OtaError::CONNECTION_FAILED, "Connection failed");
        } else {
            char errMsg[32];
            snprintf(errMsg, sizeof(errMsg), "Begin failed: %d", err);
            setError(OtaError::HTTP_ERROR, errMsg);
        }
        setState(OtaState::FAILED);
        reportComplete(false);
        return;
    }

    // Get image size
    int imageSize = esp_https_ota_get_image_size(otaHandle);
    if (imageSize > 0) {
        _bytesTotal = imageSize;
    }

    Serial.printf("[OTA] Image size: %d bytes\n", imageSize);

    setState(OtaState::WRITING);

    // Download and write in chunks
    unsigned long lastProgressTime = millis();
    size_t lastBytesWritten = 0;

    while (true) {
        // Check for cancellation
        if (_cancelRequested) {
            esp_https_ota_abort(otaHandle);
            setError(OtaError::CANCELLED, "Update cancelled");
            setState(OtaState::CANCELLED);
            reportComplete(false);
            return;
        }

        // Check timeout
        if (millis() - _startTime > OTA_TIMEOUT_SEC * 1000UL) {
            esp_https_ota_abort(otaHandle);
            setError(OtaError::TIMEOUT, "Download timeout");
            setState(OtaState::FAILED);
            reportComplete(false);
            return;
        }

        err = esp_https_ota_perform(otaHandle);

        if (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            _bytesWritten = esp_https_ota_get_image_len_read(otaHandle);

            // Report progress periodically
            if (millis() - lastProgressTime >= OTA_PROGRESS_INTERVAL_MS) {
                uint8_t percent = 0;
                if (_bytesTotal > 0) {
                    percent = (uint8_t)((_bytesWritten * 100) / _bytesTotal);
                }
                reportProgress(percent, _bytesWritten, _bytesTotal);

                // Calculate speed
                float elapsed = (millis() - lastProgressTime) / 1000.0f;
                float speed = (float)(_bytesWritten - lastBytesWritten) / elapsed / 1024.0f;
                Serial.printf("[OTA] Progress: %d%% (%d/%d bytes, %.1f KB/s)\n",
                              percent, _bytesWritten, _bytesTotal, speed);

                lastProgressTime = millis();
                lastBytesWritten = _bytesWritten;
            }

            continue;
        }

        if (err == ESP_OK) {
            // Download complete
            break;
        }

        // Error occurred
        esp_https_ota_abort(otaHandle);
        char errMsg[32];
        snprintf(errMsg, sizeof(errMsg), "Perform failed: %d", err);
        setError(OtaError::WRITE_FAILED, errMsg);
        setState(OtaState::FAILED);
        reportComplete(false);
        return;
    }

    setState(OtaState::VERIFYING);

    // Verify downloaded image
    if (!esp_https_ota_is_complete_data_received(otaHandle)) {
        esp_https_ota_abort(otaHandle);
        setError(OtaError::CHECKSUM_MISMATCH, "Incomplete data");
        setState(OtaState::FAILED);
        reportComplete(false);
        return;
    }

    // Finish OTA (validates and sets boot partition)
    err = esp_https_ota_finish(otaHandle);

    if (err == ESP_OK) {
        _progress = 100;
        reportProgress(100, _bytesWritten, _bytesTotal);
        setState(OtaState::COMPLETE);
        reportComplete(true);
        if (_autoRestart) {
            Serial.println("[OTA] Update successful! Rebooting in 5 seconds...");
            delay(5000);
            esp_restart();
        } else {
            Serial.println("[OTA] Update successful! Waiting for external restart...");
        }
    } else if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
        setError(OtaError::CHECKSUM_MISMATCH, "Image validation failed");
        setState(OtaState::FAILED);
        reportComplete(false);
    } else {
        char errMsg[32];
        snprintf(errMsg, sizeof(errMsg), "Finish failed: %d", err);
        setError(OtaError::PARTITION_ERROR, errMsg);
        setState(OtaState::FAILED);
        reportComplete(false);
    }
#else
    esp_http_client_config_t httpConfig = {};
    httpConfig.url = url;
    httpConfig.timeout_ms = OTA_TIMEOUT_SEC * 1000;
    httpConfig.keep_alive_enable = true;
    httpConfig.transport_type = HTTP_TRANSPORT_OVER_TCP;

    esp_http_client_handle_t client = esp_http_client_init(&httpConfig);
    if (!client) {
        setError(OtaError::HTTP_ERROR, "HTTP client init failed");
        setState(OtaState::FAILED);
        reportComplete(false);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        setError(OtaError::CONNECTION_FAILED, "Connection failed");
        setState(OtaState::FAILED);
        reportComplete(false);
        return;
    }

    int contentLength = esp_http_client_fetch_headers(client);
    int statusCode = esp_http_client_get_status_code(client);
    Serial.printf("[OTA] HTTP status: %d\n", statusCode);
    const char* contentType = nullptr;
    if (esp_http_client_get_header(client, "Content-Type", (char**)&contentType) == ESP_OK && contentType) {
        Serial.printf("[OTA] Content-Type: %s\n", contentType);
    }
    if (statusCode < 200 || statusCode >= 300) {
        char errBody[128];
        int n = esp_http_client_read(client, errBody, sizeof(errBody) - 1);
        if (n > 0) {
            errBody[n] = '\0';
            Serial.printf("[OTA] HTTP error body: %s\n", errBody);
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        setError(OtaError::HTTP_ERROR, "Non-200 response");
        setState(OtaState::FAILED);
        reportComplete(false);
        return;
    }
    if (contentLength > 0) {
        _bytesTotal = static_cast<size_t>(contentLength);
    } else if (_expectedSize > 0) {
        _bytesTotal = _expectedSize;
    }

    _updatePartition = _updatePartition ? _updatePartition : esp_ota_get_next_update_partition(nullptr);
    if (!_updatePartition) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        setError(OtaError::PARTITION_ERROR, "No OTA partition");
        setState(OtaState::FAILED);
        reportComplete(false);
        return;
    }

    esp_ota_handle_t updateHandle = 0;
    err = esp_ota_begin(_updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &updateHandle);
    if (err != ESP_OK) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        char errMsg[32];
        snprintf(errMsg, sizeof(errMsg), "Begin failed: %d", err);
        setError(OtaError::PARTITION_ERROR, errMsg);
        setState(OtaState::FAILED);
        reportComplete(false);
        return;
    }

    setState(OtaState::WRITING);

    const size_t bufferSize = 2048;
    uint8_t* buffer = (uint8_t*)malloc(bufferSize);
    if (!buffer) {
        esp_ota_abort(updateHandle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        setError(OtaError::WRITE_FAILED, "No memory");
        setState(OtaState::FAILED);
        reportComplete(false);
        return;
    }

    unsigned long lastProgressTime = millis();
    size_t lastBytesWritten = 0;
    _bytesWritten = 0;
    bool loggedFirstChunk = false;

    while (true) {
        if (_cancelRequested) {
            free(buffer);
            esp_ota_abort(updateHandle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            setError(OtaError::CANCELLED, "Update cancelled");
            setState(OtaState::CANCELLED);
            reportComplete(false);
            return;
        }

        if (millis() - _startTime > OTA_TIMEOUT_SEC * 1000UL) {
            free(buffer);
            esp_ota_abort(updateHandle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            setError(OtaError::TIMEOUT, "Download timeout");
            setState(OtaState::FAILED);
            reportComplete(false);
            return;
        }

        int dataRead = esp_http_client_read(client, (char*)buffer, bufferSize);
        if (dataRead < 0) {
            free(buffer);
            esp_ota_abort(updateHandle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            setError(OtaError::HTTP_ERROR, "Read failed");
            setState(OtaState::FAILED);
            reportComplete(false);
            return;
        }
        if (dataRead == 0) {
            break;
        }
        if (!loggedFirstChunk) {
            loggedFirstChunk = true;
            Serial.printf("[OTA] First bytes: %02X %02X %02X %02X\n",
                          buffer[0], buffer[1], buffer[2], buffer[3]);
        }

        err = esp_ota_write(updateHandle, buffer, dataRead);
        if (err != ESP_OK) {
            free(buffer);
            esp_ota_abort(updateHandle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            char errMsg[32];
            snprintf(errMsg, sizeof(errMsg), "Write failed: %d", err);
            setError(OtaError::WRITE_FAILED, errMsg);
            setState(OtaState::FAILED);
            reportComplete(false);
            return;
        }

        _bytesWritten += dataRead;
        if (millis() - lastProgressTime >= OTA_PROGRESS_INTERVAL_MS) {
            uint8_t percent = 0;
            if (_bytesTotal > 0) {
                percent = (uint8_t)((_bytesWritten * 100) / _bytesTotal);
            }
            reportProgress(percent, _bytesWritten, _bytesTotal);

            float elapsed = (millis() - lastProgressTime) / 1000.0f;
            float speed = (float)(_bytesWritten - lastBytesWritten) / elapsed / 1024.0f;
            Serial.printf("[OTA] Progress: %d%% (%d/%d bytes, %.1f KB/s)\n",
                          percent, _bytesWritten, _bytesTotal, speed);

            lastProgressTime = millis();
            lastBytesWritten = _bytesWritten;
        }
    }

    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (_expectedSize > 0 && _bytesWritten != _expectedSize) {
        setError(OtaError::CHECKSUM_MISMATCH, "Size mismatch");
        setState(OtaState::FAILED);
        reportComplete(false);
        return;
    }

    err = esp_ota_end(updateHandle);
    if (err != ESP_OK) {
        char errMsg[32];
        snprintf(errMsg, sizeof(errMsg), "Finish failed: %d", err);
        setError(OtaError::PARTITION_ERROR, errMsg);
        setState(OtaState::FAILED);
        reportComplete(false);
        return;
    }

    err = esp_ota_set_boot_partition(_updatePartition);
    if (err != ESP_OK) {
        char errMsg[32];
        snprintf(errMsg, sizeof(errMsg), "Boot set failed: %d", err);
        setError(OtaError::PARTITION_ERROR, errMsg);
        setState(OtaState::FAILED);
        reportComplete(false);
        return;
    }

    _progress = 100;
    reportProgress(100, _bytesWritten, _bytesTotal);
    setState(OtaState::COMPLETE);
    reportComplete(true);
    if (_autoRestart) {
        Serial.println("[OTA] Update successful! Rebooting in 5 seconds...");
        delay(5000);
        esp_restart();
    } else {
        Serial.println("[OTA] Update successful! Waiting for external restart...");
    }
#endif
}

const char* OtaHandler::stateToString(OtaState state)
{
    switch (state) {
        case OtaState::IDLE:        return "idle";
        case OtaState::STARTING:    return "starting";
        case OtaState::DOWNLOADING: return "downloading";
        case OtaState::WRITING:     return "writing";
        case OtaState::VERIFYING:   return "verifying";
        case OtaState::COMPLETE:    return "complete";
        case OtaState::FAILED:      return "failed";
        case OtaState::CANCELLED:   return "cancelled";
        default:                    return "unknown";
    }
}

const char* OtaHandler::errorToString(OtaError error)
{
    switch (error) {
        case OtaError::NONE:              return "none";
        case OtaError::INVALID_URL:       return "invalid_url";
        case OtaError::CONNECTION_FAILED: return "connection_failed";
        case OtaError::HTTP_ERROR:        return "http_error";
        case OtaError::TIMEOUT:           return "timeout";
        case OtaError::CHECKSUM_MISMATCH: return "checksum_mismatch";
        case OtaError::SIGNATURE_INVALID: return "signature_invalid";
        case OtaError::WRITE_FAILED:      return "write_failed";
        case OtaError::PARTITION_ERROR:   return "partition_error";
        case OtaError::VERSION_INVALID:   return "version_invalid";
        case OtaError::CANCELLED:         return "cancelled";
        case OtaError::UNKNOWN:           return "unknown";
        default:                          return "unknown";
    }
}

#endif // OTA_ENABLED
