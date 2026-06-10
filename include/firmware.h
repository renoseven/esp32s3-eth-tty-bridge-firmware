// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Project-wide compile-time constants (Firmware namespace): version, hardware
// pins, factory defaults, validation limits, NVS layout, network ports, and
// FreeRTOS task sizing / service timing.
//
// The REST/wire contract lives separately in restful.h.

#pragma once

#include <cstddef>
#include <cstdint>

#define FW_VERSION_NAME "v0.1a" // Human-readable release tag.

#ifndef FW_VERSION_ID
#define FW_VERSION_ID "dev" // Build id; override via build_opt.h (e.g. git hash).
#endif

#define FW_VERSION FW_VERSION_NAME " (" FW_VERSION_ID ")" // Full version string.

namespace Firmware {

    // --- Version ---

    constexpr const char* VERSION = FW_VERSION; // Firmware version for GET /status.

    // --- Device ---

    constexpr uint32_t DEVICE_REBOOT_DELAY_MS = 2000; // Delay before ESP.restart(); lets HTTP response close.

    // --- LED ---

    constexpr uint8_t LED_PIN = 21;                 // LED GPIO pin.
    constexpr uint8_t LED_BRIGHTNESS = 8;           // LED brightness scale (0–0xFF).
    constexpr uint16_t LED_FLASH_DURATION_MS = 20;  // Minimum TX/RX flash duration.
    constexpr uint16_t LED_ROTATE_INTERVAL_MS = 10; // Overlap rotation tick when multiple flashes are active.

    // --- Factory defaults ---

    constexpr const char* DEFAULT_DEVICE_NAME_PREFIX = "SerialBridge-"; // Device name prefix; MAC suffix appended.

    constexpr uint8_t DEFAULT_WIFI_PREF_MODE = 2;                           // WifiMode::Ap — start in configuration AP.
    constexpr const char* DEFAULT_WIFI_STA_SSID = "";                       // Empty = no saved STA network.
    constexpr const char* DEFAULT_WIFI_STA_PASSWORD = "";                   // Empty = open STA network allowed.
    constexpr const char* DEFAULT_WIFI_AP_SSID_PREFIX = "SerialBridge-AP-"; // AP SSID prefix; MAC suffix appended.
    constexpr const char* DEFAULT_WIFI_AP_PASSWORD = "12345678";            // Default AP WPA2 passphrase.

    constexpr const char* DEFAULT_SSH_PRIVATE_KEY = "";    // Empty = generate Ed25519 server key at first SSH start.
    constexpr const char* DEFAULT_SSH_USERNAME = "admin";  // Default SSH login name.
    constexpr const char* DEFAULT_SSH_PASSWORD = "admin";  // Default SSH password.
    constexpr const char* DEFAULT_SSH_AUTHORIZED_KEY = ""; // Empty = no client public-key auth by default.
    constexpr bool DEFAULT_SSH_ALLOW_NO_AUTH = false;      // Default: password or public-key required.

    // --- Limits ---

    constexpr size_t LIMIT_DEVICE_NAME_MIN = 1;           // Min device name length.
    constexpr size_t LIMIT_DEVICE_NAME_MAX = 32;          // Max device name length.
    constexpr size_t LIMIT_WIFI_SSID_MIN = 1;             // Min WiFi SSID length.
    constexpr size_t LIMIT_WIFI_SSID_MAX = 32;            // Max WiFi SSID length.
    constexpr size_t LIMIT_WIFI_PASSWORD_MIN = 8;         // Min WiFi STA WPA passphrase length.
    constexpr size_t LIMIT_WIFI_PASSWORD_MAX = 63;        // Max WiFi STA WPA passphrase length.
    constexpr size_t LIMIT_SSH_USERNAME_MIN = 1;          // Min SSH username length.
    constexpr size_t LIMIT_SSH_USERNAME_MAX = 32;         // Max SSH username length.
    constexpr size_t LIMIT_SSH_PASSWORD_MAX = 64;         // Max SSH password length.
    constexpr size_t LIMIT_SSH_AUTHORIZED_KEY_MAX = 1536; // Max OpenSSH authorized_keys line (RSA-8192).

    // --- Network ---

    constexpr uint16_t NET_PORT_WEB = 80; // HTTP configuration UI port.
    constexpr uint16_t NET_PORT_SSH = 22; // SSH serial gateway port.
    constexpr uint16_t NET_PORT_DNS = 53; // Captive portal DNS in AP-CONFIG mode.

    // --- Wi-Fi ---

    constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;        // STA first-connect timeout before AP fallback.
    constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000;      // STA reconnect retry while link is down.
    constexpr uint32_t WIFI_FALLBACK_AP_TIMEOUT_MS = 60000;    // STA link-down time before reopening AP.
    constexpr uint32_t WIFI_FALLBACK_STA_INTERVAL_MS = 600000; // AP mode retry interval to rejoin saved SSID.
    constexpr uint32_t WIFI_SERVICE_RESTART_DELAY_MS = 1500;   // Deferred wifi.restart() after POST /save|/reset.

    // --- Serial ---

    constexpr size_t SERIAL_BUF_SIZE = 8192;     // USB RX/TX ring buffer capacity.
    constexpr uint32_t SERIAL_TASK_STACK = 4096; // USB bridge task stack (bytes).
    constexpr uint8_t SERIAL_TASK_PRIO = 7;      // USB bridge task priority.
    constexpr uint8_t SERIAL_TASK_CORE = 1;      // USB bridge task CPU core.

    // --- SSH ---

    constexpr uint32_t SSH_TASK_STACK = 16384;             // SSH listener task stack (bytes).
    constexpr uint8_t SSH_TASK_PRIO = 5;                   // SSH listener task priority.
    constexpr uint8_t SSH_TASK_CORE = 0;                   // SSH listener task CPU core.
    constexpr uint32_t SSH_NEGOTIATE_TIMEOUT_MS = 35000;   // Max wait for the negotiation (auth + channel + shell).
    constexpr uint32_t SSH_BIND_RETRY_INTERVAL_MS = 2000;  // ssh_bind_listen() retry interval.
    constexpr uint32_t SSH_NEW_RETRY_INTERVAL_MS = 100;    // ssh_new() retry interval.
    constexpr uint32_t SSH_SERVICE_RESTART_DELAY_MS = 500; // Deferred ssh.restart() after POST /save|/reset.
    constexpr uint32_t SSH_ACCEPT_POLL_INTERVAL_MS = 50;   // Idle accept() poll when no client is connecting.

    // --- Bridge ---

    constexpr uint32_t BRIDGE_POLL_INTERVAL_MS = 1; // Fallback poll when no notification wakes the task.

} // namespace Firmware
