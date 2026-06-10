// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Web service: configuration front end.
//
// Serves the embedded single-page UI and a small REST API (see restful.h) that
// reads live status and reads/writes device, Wi-Fi, and SSH configuration. It
// holds references to the other services to read status and trigger restarts,
// and persists changes through NVS. Handlers run on the AsyncTCP callback
// context and must not block.

#pragma once

#include <ESPAsyncWebServer.h>

#include <cstddef>
#include <cstdint>

#include "firmware.h"

class Device;
class SerialService;
class SshService;
class WifiService;

// ============================================================
//                         Web Service
// ============================================================

// HTTP server hosting the embedded UI and the REST configuration API.
class WebService {
  private:
    // --- Dependencies ---

    Device& device;
    SerialService& serial;
    SshService& ssh;
    WifiService& wifi;

    // --- HTTP server ---

    AsyncWebServer server;

    // Unmatched routes (redirect to GET /).
    void handleNotFound(AsyncWebServerRequest* req);

    // GET /
    void handleIndex(AsyncWebServerRequest* req);

    // GET /style.css
    void handleStyle(AsyncWebServerRequest* req);

    // GET /script.js
    void handleScript(AsyncWebServerRequest* req);

    // GET /status
    void handleStatus(AsyncWebServerRequest* req);

    // GET /config/device/fields
    void handleDeviceConfigFields(AsyncWebServerRequest* req);

    // POST /config/device/save
    void handleSaveDeviceConfig(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t);

    // POST /config/device/reset
    void handleResetDeviceConfig(AsyncWebServerRequest* req);

    // GET /config/wifi/fields
    void handleWifiConfigFields(AsyncWebServerRequest* req);

    // POST /config/wifi/save
    void handleSaveWifiConfig(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t);

    // POST /config/wifi/reset
    void handleResetWifiConfig(AsyncWebServerRequest* req);

    // GET /config/ssh/fields
    void handleSshConfigFields(AsyncWebServerRequest* req);

    // POST /config/ssh/save
    void handleSaveSshConfig(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t);

    // POST /config/ssh/reset
    void handleResetSshConfig(AsyncWebServerRequest* req);

    // POST /factory-reset
    void handleFactoryReset(AsyncWebServerRequest* req);

    // POST /reboot
    void handleReboot(AsyncWebServerRequest* req);

  public:
    WebService(Device& device, SerialService& serial, SshService& ssh, WifiService& wifi)
        : device(device), serial(serial), ssh(ssh), wifi(wifi), server(Firmware::NET_PORT_WEB) {}

    // --- Lifecycle ---

    // Register all routes (static assets, REST API, captive-portal fallback)
    // and start listening on NET_PORT_WEB. The fallback must be registered last.
    void begin();
};
