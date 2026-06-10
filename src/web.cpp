// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// WebService implementation.
//
// Hosts the embedded UI and the configuration REST API on the async HTTP
// server. The wire contract (routes, JSON keys, errors, sizing) lives in
// restful.h and is mirrored by assets/script.js.
//
// POST config flow: parseRequest (size/JSON guards) -> validate* (field
// rules) -> update* (merge into NVS) -> respond -> schedule() a deferred
// service restart so the HTTP response can finish before the radio/SSH bounce.
// GET flows build a JsonDocument and stream it, rejecting overflowed documents.
// Handlers run on the AsyncTCP callback context, so they must not
// block; long-running effects are pushed onto the deferred timer instead.
//
// File-local helpers are grouped in the anonymous namespace below; the public
// WebService members are thin request adapters over them.

#include "web.h"

#include <ArduinoJson.h>

#include "assets.h"
#include "device.h"
#include "restful.h"
#include "serial.h"
#include "ssh.h"
#include "wifi.h"

namespace {

    // --- Response ---

    // Serve a pre-compressed static asset (gzip) with a 1-hour browser cache.
    void sendAssetResponse(AsyncWebServerRequest* req, const char* contentType, const uint8_t* data, size_t len) {
        AsyncWebServerResponse* response = req->beginResponse(Restful::HTTP_STATUS_OK, contentType, data, len);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "public, max-age=3600");
        req->send(response);
    }

    // Reply 204 No Content. Pass closeConnection = true for endpoints that
    // trigger a deferred reboot or network restart, so the client tears down
    // the socket instead of reusing a connection that is about to disappear.
    void sendOkResponse(AsyncWebServerRequest* req, bool closeConnection = false) {
        AsyncWebServerResponse* response = req->beginResponse(Restful::HTTP_STATUS_NO_CONTENT);
        if (closeConnection) {
            response->addHeader("Connection", "close");
        }
        req->send(response);
    }

    // Reply with a Restful::Error (status + application/problem+json body).
    void sendErrorResponse(AsyncWebServerRequest* req, const Restful::Error& err) {
        req->send(err.status, Restful::MIME_PROBLEM_JSON, err.body);
    }

    // Serialize a JsonDocument as the response. If the document overflowed
    // during construction, sends overflowErr instead.
    void sendJsonResponse(AsyncWebServerRequest* req, JsonDocument& doc, const Restful::Error& overflowErr) {
        if (doc.overflowed()) {
            sendErrorResponse(req, overflowErr);
            return;
        }

        AsyncResponseStream* response = req->beginResponseStream(Restful::MIME_JSON);
        serializeJson(doc, *response);
        req->send(response);
    }

    // --- Validation ---

    // Each validate* enforces the per-field rules from firmware.h limits and
    // returns nullptr when the body is acceptable, or the specific Error to send
    // back. Fields are trimmed and, where optional, an empty value is permitted.

    const Restful::Error* validateDeviceRequest(JsonObjectConst body) {
        if (!body[Restful::JSON_KEY_CONFIG_DEVICE_NAME]) {
            return &Restful::ERR_JSON_BAD_FIELD_TYPE;
        }
        if (!body[Restful::JSON_KEY_CONFIG_DEVICE_NAME].is<const char*>()) {
            return &Restful::ERR_JSON_BAD_FIELD_TYPE;
        }

        String name = body[Restful::JSON_KEY_CONFIG_DEVICE_NAME].as<String>();
        name.trim();
        const size_t n = name.length();
        if (n < Firmware::LIMIT_DEVICE_NAME_MIN || n > Firmware::LIMIT_DEVICE_NAME_MAX) {
            return &Restful::ERR_CONFIG_DEVICE_BAD_NAME;
        }

        if (!isalnum((unsigned char)name[0]) || !isalnum((unsigned char)name[n - 1])) {
            return &Restful::ERR_CONFIG_DEVICE_BAD_NAME;
        }

        for (size_t i = 0; i < n; i++) {
            const char c = name[i];
            if (!isalnum((unsigned char)c) && c != '-') {
                return &Restful::ERR_CONFIG_DEVICE_BAD_NAME;
            }
        }

        return nullptr;
    }

    const Restful::Error* validateWifiSsid(JsonObjectConst body, const char* key) {
        if (!body[key]) {
            return nullptr;
        }
        if (!body[key].is<const char*>()) {
            return &Restful::ERR_JSON_BAD_FIELD_TYPE;
        }

        const char* raw = body[key].as<const char*>();
        String ssid(raw ? raw : "");
        ssid.trim();
        if (ssid.length() < Firmware::LIMIT_WIFI_SSID_MIN || ssid.length() > Firmware::LIMIT_WIFI_SSID_MAX) {
            return &Restful::ERR_CONFIG_WIFI_BAD_SSID;
        }

        return nullptr;
    }

    const Restful::Error* validateWifiPassword(JsonObjectConst body, const char* key) {
        if (!body[key]) {
            return nullptr;
        }
        if (!body[key].is<const char*>()) {
            return &Restful::ERR_JSON_BAD_FIELD_TYPE;
        }

        const char* raw = body[key].as<const char*>();
        if (raw) {
            String trimmed(raw);
            trimmed.trim();
            const size_t len = trimmed.length();
            if (len > 0) {
                if (len < Firmware::LIMIT_WIFI_PASSWORD_MIN) {
                    return &Restful::ERR_CONFIG_WIFI_PASSWORD_TOO_SHORT;
                }
                if (len > Firmware::LIMIT_WIFI_PASSWORD_MAX) {
                    return &Restful::ERR_CONFIG_WIFI_PASSWORD_TOO_LONG;
                }
            }
        }

        return nullptr;
    }

    const Restful::Error* validateWifiRequest(JsonObjectConst body) {
        if (!body[Restful::JSON_KEY_CONFIG_WIFI_PREF_MODE] &&
            !body[Restful::JSON_KEY_CONFIG_WIFI_STA_SSID] &&
            !body[Restful::JSON_KEY_CONFIG_WIFI_STA_PASSWORD] &&
            !body[Restful::JSON_KEY_CONFIG_WIFI_AP_SSID] &&
            !body[Restful::JSON_KEY_CONFIG_WIFI_AP_PASSWORD]) {
            return &Restful::ERR_JSON_BAD_FIELD_TYPE;
        }

        if (body[Restful::JSON_KEY_CONFIG_WIFI_PREF_MODE]) {
            if (!body[Restful::JSON_KEY_CONFIG_WIFI_PREF_MODE].is<unsigned int>()) {
                return &Restful::ERR_JSON_BAD_FIELD_TYPE;
            }

            const unsigned int mode = body[Restful::JSON_KEY_CONFIG_WIFI_PREF_MODE].as<unsigned int>();
            if (mode > static_cast<unsigned int>(WifiMode::Ap)) {
                return &Restful::ERR_CONFIG_WIFI_BAD_PREF_MODE;
            }
        }

        const Restful::Error* err = nullptr;

        err = validateWifiSsid(body, Restful::JSON_KEY_CONFIG_WIFI_STA_SSID);
        if (err) {
            return err;
        }
        err = validateWifiPassword(body, Restful::JSON_KEY_CONFIG_WIFI_STA_PASSWORD);
        if (err) {
            return err;
        }
        err = validateWifiSsid(body, Restful::JSON_KEY_CONFIG_WIFI_AP_SSID);
        if (err) {
            return err;
        }
        err = validateWifiPassword(body, Restful::JSON_KEY_CONFIG_WIFI_AP_PASSWORD);
        if (err) {
            return err;
        }

        return nullptr;
    }

    const Restful::Error* validateSshRequest(JsonObjectConst body) {
        const bool allowNoAuth = body[Restful::JSON_KEY_CONFIG_SSH_ALLOW_NO_AUTH].is<bool>() &&
                                 body[Restful::JSON_KEY_CONFIG_SSH_ALLOW_NO_AUTH].as<bool>();

        if (!allowNoAuth) {
            if (!body[Restful::JSON_KEY_CONFIG_SSH_USERNAME]) {
                return &Restful::ERR_JSON_BAD_FIELD_TYPE;
            }
            if (!body[Restful::JSON_KEY_CONFIG_SSH_USERNAME].is<const char*>()) {
                return &Restful::ERR_JSON_BAD_FIELD_TYPE;
            }

            String username = body[Restful::JSON_KEY_CONFIG_SSH_USERNAME].as<String>();
            username.trim();
            if (username.length() < Firmware::LIMIT_SSH_USERNAME_MIN ||
                username.length() > Firmware::LIMIT_SSH_USERNAME_MAX) {
                return &Restful::ERR_CONFIG_SSH_BAD_USERNAME;
            }
        } else if (body[Restful::JSON_KEY_CONFIG_SSH_USERNAME]) {
            if (!body[Restful::JSON_KEY_CONFIG_SSH_USERNAME].is<const char*>()) {
                return &Restful::ERR_JSON_BAD_FIELD_TYPE;
            }

            String username = body[Restful::JSON_KEY_CONFIG_SSH_USERNAME].as<String>();
            username.trim();
            if (username.length() > Firmware::LIMIT_SSH_USERNAME_MAX) {
                return &Restful::ERR_CONFIG_SSH_BAD_USERNAME;
            }
        }

        if (body[Restful::JSON_KEY_CONFIG_SSH_PASSWORD]) {
            if (!body[Restful::JSON_KEY_CONFIG_SSH_PASSWORD].is<const char*>()) {
                return &Restful::ERR_JSON_BAD_FIELD_TYPE;
            }

            String password = body[Restful::JSON_KEY_CONFIG_SSH_PASSWORD].as<String>();
            password.trim();
            if (password.length() > Firmware::LIMIT_SSH_PASSWORD_MAX) {
                return &Restful::ERR_CONFIG_SSH_BAD_PASSWORD;
            }
        }

        if (body[Restful::JSON_KEY_CONFIG_SSH_AUTHORIZED_KEY]) {
            if (!body[Restful::JSON_KEY_CONFIG_SSH_AUTHORIZED_KEY].is<const char*>()) {
                return &Restful::ERR_JSON_BAD_FIELD_TYPE;
            }

            String authorizedKey = body[Restful::JSON_KEY_CONFIG_SSH_AUTHORIZED_KEY].as<String>();
            authorizedKey.trim();
            if (!authorizedKey.isEmpty()) {
                if (authorizedKey.length() > Firmware::LIMIT_SSH_AUTHORIZED_KEY_MAX) {
                    return &Restful::ERR_CONFIG_SSH_BAD_AUTHORIZED_KEY;
                }
                if (!SshService::isValidOpenSshPublicKey(authorizedKey)) {
                    return &Restful::ERR_CONFIG_SSH_BAD_AUTHORIZED_KEY;
                }
            }
        }

        if (body[Restful::JSON_KEY_CONFIG_SSH_ALLOW_NO_AUTH]) {
            if (!body[Restful::JSON_KEY_CONFIG_SSH_ALLOW_NO_AUTH].is<bool>()) {
                return &Restful::ERR_CONFIG_SSH_BAD_ALLOW_NO_AUTH;
            }
        }

        return nullptr;
    }

    // --- Request parsing ---

    // Parse and guard a POST body before handlers touch it. Layered checks:
    // non-empty -> within raw-size cap -> valid JSON -> root is an object ->
    // within the JsonDocument (overflowed()). Returns the matching Error on the
    // first failure, or nullptr (with doc populated) on success.
    const Restful::Error* parseRequest(JsonDocument& doc, const uint8_t* data, size_t len, size_t maxBodyLen) {
        if (len == 0) {
            return &Restful::ERR_JSON_EMPTY_BODY;
        }
        if (len > maxBodyLen) {
            return &Restful::ERR_JSON_PAYLOAD_TOO_LARGE;
        }

        const DeserializationError err = deserializeJson(doc, data, len);
        if (err == DeserializationError::NoMemory) {
            return &Restful::ERR_JSON_TOO_SMALL;
        }

        if (err) {
            return &Restful::ERR_JSON_INVALID;
        }

        if (!doc.is<JsonObject>()) {
            return &Restful::ERR_JSON_NOT_OBJECT;
        }

        if (doc.overflowed()) {
            return &Restful::ERR_JSON_TOO_SMALL;
        }

        return nullptr;
    }

    // --- Config GET field objects ---

    // Per-field constraints sent by GET /config/*/fields so the UI can
    // pre-validate inputs.  Each field object may contain { value?, required?,
    // min?, max? }; only non-zero/true values are emitted to keep the payload
    // tight.
    struct FieldSpec {
        size_t minLen = 0;
        size_t maxLen = 0;
        bool required = false;
    };

    template <typename... Args>
    void writeField(JsonObject doc, const char* key, const FieldSpec& spec, const Args&... value) {
        static_assert(sizeof...(value) <= 1, "writeField accepts at most one value");

        JsonObject field = doc[key].to<JsonObject>();

        ((field[Restful::JSON_KEY_CONFIG_FIELD_VALUE] = value), ...);
        if (spec.minLen > 0) {
            field[Restful::JSON_KEY_CONFIG_FIELD_MIN] = spec.minLen;
        }
        if (spec.maxLen > 0) {
            field[Restful::JSON_KEY_CONFIG_FIELD_MAX] = spec.maxLen;
        }
        if (spec.required) {
            field[Restful::JSON_KEY_CONFIG_FIELD_REQUIRED] = true;
        }
    }

    // --- Build GET responses ---

    JsonDocument buildStatusResponse(const Device& device, const WifiService& wifi, const SshService& ssh) {
        JsonDocument doc;

        doc[Restful::JSON_KEY_STATUS_DEVICE_NAME] = device.getName();
        doc[Restful::JSON_KEY_STATUS_DEVICE_FIRMWARE_VERSION] = Device::getFirmwareVersion();
        doc[Restful::JSON_KEY_STATUS_DEVICE_MEMORY_FREE] = Device::getFreeMemory();
        doc[Restful::JSON_KEY_STATUS_DEVICE_MEMORY_TOTAL] = Device::getTotalMemory();
        doc[Restful::JSON_KEY_STATUS_DEVICE_UPTIME] = Device::getUptime();
        doc[Restful::JSON_KEY_STATUS_WIFI_MODE] = static_cast<uint8_t>(wifi.getActiveMode());
        doc[Restful::JSON_KEY_STATUS_WIFI_RSSI] = wifi.getRssi();
        doc[Restful::JSON_KEY_STATUS_WIFI_MAC_ADDR] = wifi.getMacAddr();
        doc[Restful::JSON_KEY_STATUS_WIFI_IPV4_ADDR] = wifi.getIpv4Addr();
        doc[Restful::JSON_KEY_STATUS_WIFI_IPV6_ADDR] = wifi.getIpv6Addr();
        doc[Restful::JSON_KEY_STATUS_SSH_CONNECTED] = ssh.getConnected();
        doc[Restful::JSON_KEY_STATUS_SSH_PUBLIC_KEY] = ssh.getPublicKey();

        return doc;
    }

    JsonDocument buildDeviceFieldsResponse(const Device& device) {
        static constexpr FieldSpec DEVICE_NAME_SPEC{
            .minLen = Firmware::LIMIT_DEVICE_NAME_MIN,
            .maxLen = Firmware::LIMIT_DEVICE_NAME_MAX,
            .required = true,
        };

        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        writeField(root, Restful::JSON_KEY_CONFIG_DEVICE_NAME, DEVICE_NAME_SPEC, device.getName());

        return doc;
    }

    JsonDocument buildWifiFieldsResponse(const WifiService& wifi) {
        static constexpr FieldSpec SSID_SPEC{
            .minLen = Firmware::LIMIT_WIFI_SSID_MIN,
            .maxLen = Firmware::LIMIT_WIFI_SSID_MAX,
            .required = true,
        };
        static constexpr FieldSpec PASSWORD_SPEC{
            .minLen = Firmware::LIMIT_WIFI_PASSWORD_MIN,
            .maxLen = Firmware::LIMIT_WIFI_PASSWORD_MAX,
        };

        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        writeField(root, Restful::JSON_KEY_CONFIG_WIFI_PREF_MODE, {}, static_cast<uint8_t>(wifi.getPrefMode()));
        writeField(root, Restful::JSON_KEY_CONFIG_WIFI_STA_SSID, SSID_SPEC, wifi.getStaSsid());
        writeField(root, Restful::JSON_KEY_CONFIG_WIFI_STA_PASSWORD, PASSWORD_SPEC);
        writeField(root, Restful::JSON_KEY_CONFIG_WIFI_AP_SSID, SSID_SPEC, wifi.getApSsid());
        writeField(root, Restful::JSON_KEY_CONFIG_WIFI_AP_PASSWORD, PASSWORD_SPEC);

        return doc;
    }

    JsonDocument buildSshFieldsResponse(const SshService& ssh) {
        static constexpr FieldSpec USERNAME_SPEC{
            .minLen = Firmware::LIMIT_SSH_USERNAME_MIN,
            .maxLen = Firmware::LIMIT_SSH_USERNAME_MAX,
        };
        static constexpr FieldSpec PASSWORD_SPEC{
            .maxLen = Firmware::LIMIT_SSH_PASSWORD_MAX,
        };
        static constexpr FieldSpec AUTHORIZED_KEY_SPEC{
            .maxLen = Firmware::LIMIT_SSH_AUTHORIZED_KEY_MAX,
        };

        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        writeField(root, Restful::JSON_KEY_CONFIG_SSH_USERNAME, USERNAME_SPEC, ssh.getUsername());
        writeField(root, Restful::JSON_KEY_CONFIG_SSH_PASSWORD, PASSWORD_SPEC);
        writeField(root, Restful::JSON_KEY_CONFIG_SSH_AUTHORIZED_KEY, AUTHORIZED_KEY_SPEC, ssh.getAuthorizedKey());
        writeField(root, Restful::JSON_KEY_CONFIG_SSH_ALLOW_NO_AUTH, {}, ssh.getAllowNoAuth());

        return doc;
    }

    // --- POST persist ---

    // Each update* reads the current config, overwrites only the fields present
    // in the request (partial update), and writes it back. Callers must have run
    // the matching validate* first.

    void updateDeviceConfig(JsonObjectConst body) {
        DeviceConfig config = DeviceConfig::read();

        String name = config.name;
        if (body[Restful::JSON_KEY_CONFIG_DEVICE_NAME].is<const char*>()) {
            name = body[Restful::JSON_KEY_CONFIG_DEVICE_NAME].as<const char*>();
            name.trim();
        }
        config.name = name;

        config.write();
    }

    void updateWifiConfig(JsonObjectConst body) {
        WifiConfig config = WifiConfig::read();

        if (body[Restful::JSON_KEY_CONFIG_WIFI_PREF_MODE].is<unsigned int>()) {
            config.prefMode = static_cast<WifiMode>(body[Restful::JSON_KEY_CONFIG_WIFI_PREF_MODE].as<unsigned int>());
        }
        if (body[Restful::JSON_KEY_CONFIG_WIFI_STA_SSID].is<const char*>()) {
            String v = body[Restful::JSON_KEY_CONFIG_WIFI_STA_SSID].as<const char*>();
            v.trim();
            config.staSsid = v;
        }
        if (body[Restful::JSON_KEY_CONFIG_WIFI_STA_PASSWORD].is<const char*>()) {
            String v = body[Restful::JSON_KEY_CONFIG_WIFI_STA_PASSWORD].as<const char*>();
            v.trim();
            config.staPassword = v;
        }
        if (body[Restful::JSON_KEY_CONFIG_WIFI_AP_SSID].is<const char*>()) {
            String v = body[Restful::JSON_KEY_CONFIG_WIFI_AP_SSID].as<const char*>();
            v.trim();
            config.apSsid = v;
        }
        if (body[Restful::JSON_KEY_CONFIG_WIFI_AP_PASSWORD].is<const char*>()) {
            String v = body[Restful::JSON_KEY_CONFIG_WIFI_AP_PASSWORD].as<const char*>();
            v.trim();
            config.apPassword = v;
        }

        config.write();
    }

    void updateSshConfig(JsonObjectConst body) {
        SshConfig config = SshConfig::read();

        if (body[Restful::JSON_KEY_CONFIG_SSH_USERNAME].is<const char*>()) {
            String username = body[Restful::JSON_KEY_CONFIG_SSH_USERNAME].as<const char*>();
            username.trim();
            config.username = username;
        }
        if (body[Restful::JSON_KEY_CONFIG_SSH_PASSWORD].is<const char*>()) {
            String password = body[Restful::JSON_KEY_CONFIG_SSH_PASSWORD].as<const char*>();
            password.trim();
            config.password = password;
        }
        if (body[Restful::JSON_KEY_CONFIG_SSH_AUTHORIZED_KEY].is<const char*>()) {
            String authorizedKey = body[Restful::JSON_KEY_CONFIG_SSH_AUTHORIZED_KEY].as<const char*>();
            authorizedKey.trim();
            config.authorizedKey = authorizedKey;
        }
        if (body[Restful::JSON_KEY_CONFIG_SSH_ALLOW_NO_AUTH].is<bool>()) {
            config.allowNoAuth = body[Restful::JSON_KEY_CONFIG_SSH_ALLOW_NO_AUTH].as<bool>();
        }
        // Never persist an empty username: even in no-auth mode a valid login
        // name must remain so password/key auth still works after toggling back.
        if (config.username.isEmpty()) {
            config.username = Firmware::DEFAULT_SSH_USERNAME;
        }

        config.write();
    }

    // --- Deferred actions ---

    // Run callback once, delayMs from now, off the HTTP callback context (on the
    // esp_timer task). This lets a handler reply first and reboot/restart after.
    //
    // A single shared one-shot timer backs all deferrals. If another action is
    // still pending, the two are chained (prev then next) and the longer delay
    // wins, so concurrent requests cannot drop each other's effect. Relies on
    // the AsyncTCP single-threaded callback model; not safe for arbitrary
    // multi-threaded callers.
    void schedule(uint32_t delayMs, std::function<void()> callback) {
        static std::function<void()> action;
        static uint64_t delayUsPending = 0;
        static esp_timer_handle_t timer = nullptr;

        const uint64_t delayUs = static_cast<uint64_t>(delayMs) * 1000;

        if (action) {
            std::function<void()> prev = std::move(action);
            action = [prev = std::move(prev), next = std::move(callback)]() {
                prev();
                next();
            };
            if (delayUs > delayUsPending) {
                delayUsPending = delayUs;
            }
        } else {
            action = std::move(callback);
            delayUsPending = delayUs;
        }

        if (timer == nullptr) {
            const esp_timer_create_args_t TIMER_ARGS{
                .callback =
                    +[](void*) {
                        if (action) {
                            action();
                            action = nullptr;
                            delayUsPending = 0;
                        }
                    },
                .arg = nullptr,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "schedule_task",
                .skip_unhandled_events = false,
            };
            esp_timer_create(&TIMER_ARGS, &timer);
        }

        esp_timer_stop(timer);
        esp_timer_start_once(timer, delayUsPending);
    }

    // Placeholder onRequest handler for POST routes. The body arrives via the
    // upload/body callback (handleSave*), which sends the response; AsyncWebServer
    // still requires a non-null request handler, so this intentionally does
    // nothing.
    void handlePost(AsyncWebServerRequest*) {}

} // namespace

// ============================================================
//                         Web Service
// ============================================================

// Unmatched routes (redirect to GET /).
void WebService::handleNotFound(AsyncWebServerRequest* req) {
    req->redirect(Restful::ROUTE_INDEX);
}

// GET /
void WebService::handleIndex(AsyncWebServerRequest* req) {
    sendAssetResponse(req, Restful::MIME_HTML, Assets::INDEX_HTML, Assets::INDEX_HTML_LEN);
}

// GET /style.css
void WebService::handleStyle(AsyncWebServerRequest* req) {
    sendAssetResponse(req, Restful::MIME_CSS, Assets::STYLE_CSS, Assets::STYLE_CSS_LEN);
}

// GET /script.js
void WebService::handleScript(AsyncWebServerRequest* req) {
    sendAssetResponse(req, Restful::MIME_JS, Assets::SCRIPT_JS, Assets::SCRIPT_JS_LEN);
}

// GET /status
void WebService::handleStatus(AsyncWebServerRequest* req) {
    JsonDocument doc = buildStatusResponse(this->device, this->wifi, this->ssh);
    sendJsonResponse(req, doc, Restful::ERR_STATUS_OVERFLOW);
}

// GET /config/device/fields
void WebService::handleDeviceConfigFields(AsyncWebServerRequest* req) {
    JsonDocument doc = buildDeviceFieldsResponse(this->device);
    sendJsonResponse(req, doc, Restful::ERR_CONFIG_DEVICE_OVERFLOW);
}

// POST /config/device/save
void WebService::handleSaveDeviceConfig(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    JsonDocument doc;

    const Restful::Error* err = parseRequest(doc, data, len, Restful::POST_BODY_MAX_CONFIG_DEVICE_SAVE);
    if (err) {
        sendErrorResponse(req, *err);
        return;
    }

    const JsonObjectConst body = doc.as<JsonObjectConst>();
    err = validateDeviceRequest(body);
    if (err) {
        sendErrorResponse(req, *err);
        return;
    }

    updateDeviceConfig(body);
    sendOkResponse(req, true);
    schedule(Firmware::WIFI_SERVICE_RESTART_DELAY_MS, [this]() {
        this->device.reload();
        this->wifi.restart();
    });
}

// POST /config/device/reset
void WebService::handleResetDeviceConfig(AsyncWebServerRequest* req) {
    DeviceConfig::clear();
    sendOkResponse(req, true);
    schedule(Firmware::WIFI_SERVICE_RESTART_DELAY_MS, [this]() {
        this->device.reload();
        this->wifi.restart();
    });
}

// GET /config/wifi/fields
void WebService::handleWifiConfigFields(AsyncWebServerRequest* req) {
    JsonDocument doc = buildWifiFieldsResponse(this->wifi);
    sendJsonResponse(req, doc, Restful::ERR_CONFIG_WIFI_OVERFLOW);
}

// POST /config/wifi/save
void WebService::handleSaveWifiConfig(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    JsonDocument doc;
    const Restful::Error* err = parseRequest(doc, data, len, Restful::POST_BODY_MAX_CONFIG_WIFI_SAVE);
    if (err) {
        sendErrorResponse(req, *err);
        return;
    }

    const JsonObjectConst body = doc.as<JsonObjectConst>();
    err = validateWifiRequest(body);
    if (err) {
        sendErrorResponse(req, *err);
        return;
    }

    updateWifiConfig(body);
    sendOkResponse(req, true);
    schedule(Firmware::WIFI_SERVICE_RESTART_DELAY_MS, [this]() { this->wifi.restart(); });
}

// POST /config/wifi/reset
void WebService::handleResetWifiConfig(AsyncWebServerRequest* req) {
    WifiConfig::clear();
    sendOkResponse(req, true);
    schedule(Firmware::WIFI_SERVICE_RESTART_DELAY_MS, [this]() { this->wifi.restart(); });
}

// GET /config/ssh/fields
void WebService::handleSshConfigFields(AsyncWebServerRequest* req) {
    JsonDocument doc = buildSshFieldsResponse(this->ssh);
    sendJsonResponse(req, doc, Restful::ERR_CONFIG_SSH_OVERFLOW);
}

// POST /config/ssh/save
void WebService::handleSaveSshConfig(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    JsonDocument doc;

    const Restful::Error* err = parseRequest(doc, data, len, Restful::POST_BODY_MAX_CONFIG_SSH_SAVE);
    if (err) {
        sendErrorResponse(req, *err);
        return;
    }

    const JsonObjectConst body = doc.as<JsonObjectConst>();
    err = validateSshRequest(body);
    if (err) {
        sendErrorResponse(req, *err);
        return;
    }

    updateSshConfig(body);
    sendOkResponse(req);
    schedule(Firmware::SSH_SERVICE_RESTART_DELAY_MS, [this]() { this->ssh.restart(); });
}

// POST /config/ssh/reset
void WebService::handleResetSshConfig(AsyncWebServerRequest* req) {
    SshConfig::clear();
    sendOkResponse(req);
    schedule(Firmware::SSH_SERVICE_RESTART_DELAY_MS, [this]() { this->ssh.restart(); });
}

// POST /factory-reset
void WebService::handleFactoryReset(AsyncWebServerRequest* req) {
    DeviceConfig::clear();
    WifiConfig::clear();
    SshConfig::clear();
    sendOkResponse(req, true);
    schedule(Firmware::DEVICE_REBOOT_DELAY_MS, []() { Device::reboot(); });
}

// POST /reboot
void WebService::handleReboot(AsyncWebServerRequest* req) {
    sendOkResponse(req, true);
    schedule(Firmware::DEVICE_REBOOT_DELAY_MS, []() { Device::reboot(); });
}

// --- Lifecycle ---

void WebService::begin() {
    // --- Static assets ---

    this->server.on(Restful::ROUTE_INDEX, HTTP_GET, [this](AsyncWebServerRequest* req) { this->handleIndex(req); });
    this->server.on(Restful::ROUTE_STYLE, HTTP_GET, [this](AsyncWebServerRequest* req) { this->handleStyle(req); });
    this->server.on(Restful::ROUTE_SCRIPT, HTTP_GET, [this](AsyncWebServerRequest* req) { this->handleScript(req); });

    // --- REST API ---

    // GET /status
    this->server.on(Restful::ROUTE_STATUS, HTTP_GET, [this](AsyncWebServerRequest* req) { this->handleStatus(req); });

    // GET /config/device/fields; POST /config/device/save|reset
    this->server.on(Restful::ROUTE_CONFIG_DEVICE_FIELDS, HTTP_GET,
                    [this](AsyncWebServerRequest* req) { this->handleDeviceConfigFields(req); });
    this->server.on(Restful::ROUTE_CONFIG_DEVICE_SAVE, HTTP_POST, handlePost, nullptr,
                    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
                        this->handleSaveDeviceConfig(req, data, len, index, total);
                    });
    this->server.on(Restful::ROUTE_CONFIG_DEVICE_RESET, HTTP_POST,
                    [this](AsyncWebServerRequest* req) { this->handleResetDeviceConfig(req); });

    // GET /config/wifi/fields; POST /config/wifi/save|reset
    this->server.on(Restful::ROUTE_CONFIG_WIFI_FIELDS, HTTP_GET,
                    [this](AsyncWebServerRequest* req) { this->handleWifiConfigFields(req); });
    this->server.on(Restful::ROUTE_CONFIG_WIFI_SAVE, HTTP_POST, handlePost, nullptr,
                    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
                        this->handleSaveWifiConfig(req, data, len, index, total);
                    });
    this->server.on(Restful::ROUTE_CONFIG_WIFI_RESET, HTTP_POST,
                    [this](AsyncWebServerRequest* req) { this->handleResetWifiConfig(req); });

    // GET /config/ssh/fields; POST /config/ssh/save|reset
    this->server.on(Restful::ROUTE_CONFIG_SSH_FIELDS, HTTP_GET,
                    [this](AsyncWebServerRequest* req) { this->handleSshConfigFields(req); });
    this->server.on(Restful::ROUTE_CONFIG_SSH_SAVE, HTTP_POST, handlePost, nullptr,
                    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
                        this->handleSaveSshConfig(req, data, len, index, total);
                    });
    this->server.on(Restful::ROUTE_CONFIG_SSH_RESET, HTTP_POST,
                    [this](AsyncWebServerRequest* req) { this->handleResetSshConfig(req); });

    // POST /factory-reset, POST /reboot
    this->server.on(Restful::ROUTE_FACTORY_RESET, HTTP_POST,
                    [this](AsyncWebServerRequest* req) { this->handleFactoryReset(req); });
    this->server.on(Restful::ROUTE_REBOOT, HTTP_POST, [this](AsyncWebServerRequest* req) { this->handleReboot(req); });

    // --- Fallback --- (must register after all routes)

    this->server.onNotFound([this](AsyncWebServerRequest* req) { this->handleNotFound(req); });

    this->server.begin();
}
