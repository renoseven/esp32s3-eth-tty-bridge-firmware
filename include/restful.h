// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Web API contract: REST routes, JSON wire keys, error responses, and POST body limits
// shared by WebService and the embedded UI.

#pragma once

#include "firmware.h"

// Keep ROUTE_* and JSON wire keys in sync with assets/script.js.
// Keep ERR_*.body "type" values in sync with the I18N err section in assets/script.js.
//
// Section order (type -> endpoint within each block):
//   HTTP transport   — status codes, Content-Type
//   Error type       — struct Error (RFC 7807 subset)
//   REST routes      — static assets -> status -> config -> system
//   JSON wire keys   — field metadata -> status -> config
//   POST body limits — POST /config/*/save only (server-side)
//   Error responses  — parse errors -> status -> config

namespace Restful {
    // ------------------------------------------------------------
    // HTTP transport
    // ------------------------------------------------------------
    // HTTP_STATUS_* mirrored in assets/script.js; keep both in sync.
    // ------------------------------------------------------------

    constexpr int HTTP_STATUS_OK = 200;
    constexpr int HTTP_STATUS_NO_CONTENT = 204;
    constexpr int HTTP_STATUS_BAD_REQUEST = 400;
    constexpr int HTTP_STATUS_NOT_FOUND = 404;
    constexpr int HTTP_STATUS_UNPROCESSABLE_ENTITY = 422;
    constexpr int HTTP_STATUS_INTERNAL_SERVER_ERROR = 500;

    constexpr const char* MIME_HTML = "text/html; charset=utf-8";
    constexpr const char* MIME_CSS = "text/css; charset=utf-8";
    constexpr const char* MIME_JS = "application/javascript; charset=utf-8";
    constexpr const char* MIME_JSON = "application/json; charset=utf-8";
    constexpr const char* MIME_PROBLEM_JSON = "application/problem+json; charset=utf-8";

    // ------------------------------------------------------------
    // Error type
    // ------------------------------------------------------------
    // POST success: HTTP 204, no body.
    // GET success:  HTTP 200, application/json body.
    // Error:        HTTP status in Error.status, application/problem+json body {"type":"<err key>"}.
    //               <err key> matches the I18N err section in assets/script.js.
    // ------------------------------------------------------------

    struct Error {
        int status;       // HTTP status code
        const char* body; // pre-built application/problem+json payload
    };

    // ------------------------------------------------------------
    // REST routes
    // ------------------------------------------------------------
    // Mirror in assets/script.js ROUTE_*.
    // ------------------------------------------------------------

    // --- Static assets ---

    constexpr const char* ROUTE_INDEX = "/";
    constexpr const char* ROUTE_STYLE = "/style.css";
    constexpr const char* ROUTE_SCRIPT = "/script.js";

    // --- GET /status ---

    constexpr const char* ROUTE_STATUS = "/status";

    // --- GET /config/device/fields ---

    constexpr const char* ROUTE_CONFIG_DEVICE_FIELDS = "/config/device/fields";

    // --- POST /config/device/save ---

    constexpr const char* ROUTE_CONFIG_DEVICE_SAVE = "/config/device/save";

    // --- POST /config/device/reset ---

    constexpr const char* ROUTE_CONFIG_DEVICE_RESET = "/config/device/reset";

    // --- GET /config/wifi/fields ---

    constexpr const char* ROUTE_CONFIG_WIFI_FIELDS = "/config/wifi/fields";

    // --- POST /config/wifi/save ---

    constexpr const char* ROUTE_CONFIG_WIFI_SAVE = "/config/wifi/save";

    // --- POST /config/wifi/reset ---

    constexpr const char* ROUTE_CONFIG_WIFI_RESET = "/config/wifi/reset";

    // --- GET /config/ssh/fields ---

    constexpr const char* ROUTE_CONFIG_SSH_FIELDS = "/config/ssh/fields";

    // --- POST /config/ssh/save ---

    constexpr const char* ROUTE_CONFIG_SSH_SAVE = "/config/ssh/save";

    // --- POST /config/ssh/reset ---

    constexpr const char* ROUTE_CONFIG_SSH_RESET = "/config/ssh/reset";

    // --- POST /factory-reset ---

    constexpr const char* ROUTE_FACTORY_RESET = "/factory-reset";

    // --- POST /reboot ---

    constexpr const char* ROUTE_REBOOT = "/reboot";

    // ------------------------------------------------------------
    // JSON wire keys
    // ------------------------------------------------------------
    // C++ name: JSON_KEY_<area>_<field>   Wire name: camelCase
    // HTML form name attributes match POST /config/* keys.
    // ------------------------------------------------------------

    // --- GET /config/*/fields — field metadata ---

    constexpr const char* JSON_KEY_CONFIG_FIELD_VALUE = "value";
    constexpr const char* JSON_KEY_CONFIG_FIELD_REQUIRED = "required";
    constexpr const char* JSON_KEY_CONFIG_FIELD_MIN = "min";
    constexpr const char* JSON_KEY_CONFIG_FIELD_MAX = "max";

    // --- GET /status ---

    constexpr const char* JSON_KEY_STATUS_DEVICE_NAME = "deviceName";
    constexpr const char* JSON_KEY_STATUS_DEVICE_FIRMWARE_VERSION = "deviceFirmwareVersion";
    constexpr const char* JSON_KEY_STATUS_DEVICE_MEMORY_FREE = "deviceMemoryFree";
    constexpr const char* JSON_KEY_STATUS_DEVICE_MEMORY_TOTAL = "deviceMemoryTotal";
    constexpr const char* JSON_KEY_STATUS_DEVICE_UPTIME = "deviceUptime";
    constexpr const char* JSON_KEY_STATUS_WIFI_MODE = "wifiMode";
    constexpr const char* JSON_KEY_STATUS_WIFI_RSSI = "wifiRssi";
    constexpr const char* JSON_KEY_STATUS_WIFI_MAC_ADDR = "wifiMacAddr";
    constexpr const char* JSON_KEY_STATUS_WIFI_IPV4_ADDR = "wifiIpv4Addr";
    constexpr const char* JSON_KEY_STATUS_WIFI_IPV6_ADDR = "wifiIpv6Addr";
    constexpr const char* JSON_KEY_STATUS_SSH_CONNECTED = "sshConnected";
    constexpr const char* JSON_KEY_STATUS_SSH_PUBLIC_KEY = "sshPublicKey";

    // --- GET/POST /config/device/* ---

    constexpr const char* JSON_KEY_CONFIG_DEVICE_NAME = "name";

    // --- GET/POST /config/wifi/* ---

    constexpr const char* JSON_KEY_CONFIG_WIFI_PREF_MODE = "prefMode";
    constexpr const char* JSON_KEY_CONFIG_WIFI_STA_SSID = "staSsid";
    constexpr const char* JSON_KEY_CONFIG_WIFI_STA_PASSWORD = "staPassword";
    constexpr const char* JSON_KEY_CONFIG_WIFI_AP_SSID = "apSsid";
    constexpr const char* JSON_KEY_CONFIG_WIFI_AP_PASSWORD = "apPassword";

    // --- GET/POST /config/ssh/* ---

    constexpr const char* JSON_KEY_CONFIG_SSH_USERNAME = "username";
    constexpr const char* JSON_KEY_CONFIG_SSH_PASSWORD = "password";
    constexpr const char* JSON_KEY_CONFIG_SSH_AUTHORIZED_KEY = "authorizedKey";
    constexpr const char* JSON_KEY_CONFIG_SSH_ALLOW_NO_AUTH = "allowNoAuth";

    // ------------------------------------------------------------
    // POST body limits
    // ------------------------------------------------------------
    // Not part of the on-wire API. Raw POST body byte bounds per save route.
    //
    //   POST_BODY_MIN_* — body size with empty string values (wire syntax only)
    //   POST_BODY_MAX_* — body size at field string limits (MIN + string limits)
    // ------------------------------------------------------------

    // --- POST /config/device/save ---
    // Body: {"name":""}

    constexpr size_t POST_BODY_MIN_CONFIG_DEVICE_SAVE = 16;
    constexpr size_t POST_BODY_MAX_CONFIG_DEVICE_SAVE =
        POST_BODY_MIN_CONFIG_DEVICE_SAVE + Firmware::LIMIT_DEVICE_NAME_MAX;

    // --- POST /config/wifi/save ---
    // Body: {"prefMode":0,"staSsid":"","staPassword":"","apSsid":"","apPassword":""}

    constexpr size_t POST_BODY_MIN_CONFIG_WIFI_SAVE = 72;
    constexpr size_t POST_BODY_MAX_CONFIG_WIFI_SAVE =
        POST_BODY_MIN_CONFIG_WIFI_SAVE + Firmware::LIMIT_WIFI_SSID_MAX * 2 + Firmware::LIMIT_WIFI_PASSWORD_MAX * 2;

    // --- POST /config/ssh/save ---
    // Body: {"username":"","password":"","authorizedKey":"","allowNoAuth":false}

    constexpr size_t POST_BODY_MIN_CONFIG_SSH_SAVE = 69;
    constexpr size_t POST_BODY_MAX_CONFIG_SSH_SAVE = POST_BODY_MIN_CONFIG_SSH_SAVE + Firmware::LIMIT_SSH_USERNAME_MAX +
                                                     Firmware::LIMIT_SSH_PASSWORD_MAX +
                                                     Firmware::LIMIT_SSH_AUTHORIZED_KEY_MAX;

    // ------------------------------------------------------------
    // Error responses
    // ------------------------------------------------------------

    // --- POST parse errors ---

    constexpr Error ERR_JSON_PAYLOAD_TOO_LARGE = {HTTP_STATUS_BAD_REQUEST, R"({"type":"jsonPayloadTooLarge"})"};
    constexpr Error ERR_JSON_EMPTY_BODY = {HTTP_STATUS_BAD_REQUEST, R"({"type":"jsonEmptyBody"})"};
    constexpr Error ERR_JSON_TOO_SMALL = {HTTP_STATUS_BAD_REQUEST, R"({"type":"jsonTooSmall"})"};
    constexpr Error ERR_JSON_INVALID = {HTTP_STATUS_BAD_REQUEST, R"({"type":"jsonInvalid"})"};
    constexpr Error ERR_JSON_NOT_OBJECT = {HTTP_STATUS_BAD_REQUEST, R"({"type":"jsonNotObject"})"};
    constexpr Error ERR_JSON_BAD_FIELD_TYPE = {HTTP_STATUS_BAD_REQUEST, R"({"type":"jsonBadFieldType"})"};

    // --- GET /status ---

    constexpr Error ERR_STATUS_OVERFLOW = {HTTP_STATUS_INTERNAL_SERVER_ERROR, R"({"type":"statusOverflow"})"};

    // --- GET /config/device/fields ---

    constexpr Error ERR_CONFIG_DEVICE_OVERFLOW = {HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                                  R"({"type":"configDeviceOverflow"})"};

    // --- POST /config/device/save ---

    constexpr Error ERR_CONFIG_DEVICE_BAD_NAME = {HTTP_STATUS_UNPROCESSABLE_ENTITY,
                                                  R"({"type":"configDeviceBadName"})"};

    // --- GET /config/wifi/fields ---

    constexpr Error ERR_CONFIG_WIFI_OVERFLOW = {HTTP_STATUS_INTERNAL_SERVER_ERROR, R"({"type":"configWifiOverflow"})"};

    // --- POST /config/wifi/save ---

    constexpr Error ERR_CONFIG_WIFI_BAD_PREF_MODE = {HTTP_STATUS_UNPROCESSABLE_ENTITY,
                                                     R"({"type":"configWifiBadPrefMode"})"};
    constexpr Error ERR_CONFIG_WIFI_BAD_SSID = {HTTP_STATUS_UNPROCESSABLE_ENTITY, R"({"type":"configWifiBadSsid"})"};
    constexpr Error ERR_CONFIG_WIFI_PASSWORD_TOO_SHORT = {HTTP_STATUS_UNPROCESSABLE_ENTITY,
                                                          R"({"type":"configWifiPasswordTooShort"})"};
    constexpr Error ERR_CONFIG_WIFI_PASSWORD_TOO_LONG = {HTTP_STATUS_UNPROCESSABLE_ENTITY,
                                                         R"({"type":"configWifiPasswordTooLong"})"};

    // --- GET /config/ssh/fields ---

    constexpr Error ERR_CONFIG_SSH_OVERFLOW = {HTTP_STATUS_INTERNAL_SERVER_ERROR, R"({"type":"configSshOverflow"})"};

    // --- POST /config/ssh/save ---

    constexpr Error ERR_CONFIG_SSH_BAD_USERNAME = {HTTP_STATUS_UNPROCESSABLE_ENTITY,
                                                   R"({"type":"configSshBadUsername"})"};
    constexpr Error ERR_CONFIG_SSH_BAD_PASSWORD = {HTTP_STATUS_UNPROCESSABLE_ENTITY,
                                                   R"({"type":"configSshBadPassword"})"};
    constexpr Error ERR_CONFIG_SSH_BAD_AUTHORIZED_KEY = {HTTP_STATUS_UNPROCESSABLE_ENTITY,
                                                         R"({"type":"configSshBadAuthorizedKey"})"};
    constexpr Error ERR_CONFIG_SSH_BAD_ALLOW_NO_AUTH = {HTTP_STATUS_UNPROCESSABLE_ENTITY,
                                                        R"({"type":"configSshBadAllowNoAuth"})"};

} // namespace Restful
