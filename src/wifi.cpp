// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// WifiService implementation.
//
// Purpose: keep the device reachable by driving a small connectivity state
// machine over the Arduino WiFi singleton. Exactly one radio mode is active at
// a time (Off / STA / configuration-AP), inferred from WiFi.getMode() rather
// than a duplicated member, so the hardware remains the single source of truth.
//
// Failover model (all timers are millis() anchors, 0 meaning "inactive"):
//   - With a saved SSID: enter STA. If the first connect exceeds
//     WIFI_CONNECT_TIMEOUT_MS, fall back to AP. Once associated, an idle radio
//     that drops link reconnects every WIFI_RECONNECT_INTERVAL_MS; if it stays
//     down for WIFI_FALLBACK_AP_TIMEOUT_MS, reopen AP for configuration.
//   - Without a saved SSID: stay in configuration-AP and periodically
//     (WIFI_FALLBACK_STA_INTERVAL_MS) retry STA in case credentials appeared.
//
// Threading: all methods run on the Arduino loop task; no internal locking.

#include "wifi.h"

#include <Preferences.h>
#include <WiFi.h>
#include <mutex>

#include "device.h"
#include "firmware.h"

namespace {
    std::mutex wifiNvsMutex;

    constexpr const char* NVS_NAMESPACE = "wifi";
    constexpr const char* NVS_KEY_PREF_MODE = "pref_mode";
    constexpr const char* NVS_KEY_STA_SSID = "sta_ssid";
    constexpr const char* NVS_KEY_STA_PASSWORD = "sta_pass";
    constexpr const char* NVS_KEY_AP_SSID = "ap_ssid";
    constexpr const char* NVS_KEY_AP_PASSWORD = "ap_pass";

    // Default AP SSID: base name + device ID.
    String defaultApSsid() {
        char buf[Firmware::LIMIT_WIFI_SSID_MAX + 1];
        snprintf(buf, sizeof(buf), "%s%04X", Firmware::DEFAULT_WIFI_AP_SSID_PREFIX, Device::getDeviceId());
        return String(buf);
    }

} // namespace

// ============================================================
//                        Wi-Fi Config
// ============================================================

WifiConfig WifiConfig::read() {
    std::lock_guard<std::mutex> lock(wifiNvsMutex);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    WifiConfig cfg{
        .prefMode = static_cast<WifiMode>(prefs.getUChar(NVS_KEY_PREF_MODE, Firmware::DEFAULT_WIFI_PREF_MODE)),
        .apSsid = prefs.getString(NVS_KEY_AP_SSID, defaultApSsid()),
        .apPassword = prefs.getString(NVS_KEY_AP_PASSWORD, Firmware::DEFAULT_WIFI_AP_PASSWORD),
        .staSsid = prefs.getString(NVS_KEY_STA_SSID, Firmware::DEFAULT_WIFI_STA_SSID),
        .staPassword = prefs.getString(NVS_KEY_STA_PASSWORD, Firmware::DEFAULT_WIFI_STA_PASSWORD),
    };
    prefs.end();

    return cfg;
}

void WifiConfig::write() const {
    std::lock_guard<std::mutex> lock(wifiNvsMutex);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar(NVS_KEY_PREF_MODE, static_cast<uint8_t>(this->prefMode));
    prefs.putString(NVS_KEY_AP_SSID, this->apSsid);
    prefs.putString(NVS_KEY_AP_PASSWORD, this->apPassword);
    prefs.putString(NVS_KEY_STA_SSID, this->staSsid);
    prefs.putString(NVS_KEY_STA_PASSWORD, this->staPassword);
    prefs.end();
}

void WifiConfig::clear() {
    std::lock_guard<std::mutex> lock(wifiNvsMutex);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
}

// ============================================================
//                       Wi-Fi Service
// ============================================================

// --- Helpers ---

void WifiService::updateNetworkAddrs() {
    switch (this->getActiveMode()) {
    case WifiMode::Ap:
        this->macAddr = WiFi.softAPmacAddress();
        this->ipv4Addr = WiFi.softAPIP().toString();
        this->ipv6Addr = WiFi.softAPlinkLocalIPv6().toString();
        break;
    case WifiMode::Sta:
        this->macAddr = WiFi.macAddress();
        this->ipv4Addr = WiFi.localIP().toString();
        this->ipv6Addr = WiFi.linkLocalIPv6().toString();
        break;
    case WifiMode::Off:
        this->macAddr = UNASSIGNED_MAC;
        this->ipv4Addr = IPAddress(IPv4).toString();
        this->ipv6Addr = IPAddress(IPv6).toString();
        break;
    }
}

// --- Mode management ---

void WifiService::resetMode() {
    switch (this->getActiveMode()) {
    case WifiMode::Ap:
        this->captiveDns.stop();
        WiFi.softAPdisconnect(true);
        break;
    case WifiMode::Sta:
        WiFi.disconnect(true);
        break;
    case WifiMode::Off:
        break;
    }

    WiFi.mode(WIFI_OFF);
    this->resetTimers();
    this->updateNetworkAddrs();
}

void WifiService::switchMode() {
    WifiMode targetMode = this->prefMode;

    // If the preferred mode is STA and the SSID is empty, fall back to AP.
    if (targetMode == WifiMode::Sta && this->staSsid.isEmpty()) {
        targetMode = WifiMode::Ap;
    }

    // If the current mode is the same as the target, do nothing.
    if (this->getActiveMode() == targetMode) {
        return;
    }

    // Otherwise, reset the current mode and start the target mode.
    switch (targetMode) {
    case WifiMode::Off:
        this->resetMode();
        break;
    case WifiMode::Sta:
        this->startStaMode();
        break;
    case WifiMode::Ap:
        this->startApMode();
        break;
    }
}

void WifiService::startApMode() {
    this->resetMode();

    WiFi.mode(WIFI_AP);
    WiFi.softAP(this->apSsid.c_str(), this->apPassword.c_str());
    WiFi.softAPenableIPv6();

    this->captiveDns.start(Firmware::NET_PORT_DNS, "*", WiFi.softAPIP());
    this->setRecoveryTime(millis());
}

void WifiService::startStaMode() {
    this->resetMode();

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(this->staSsid.c_str(), this->staPassword.c_str());
    WiFi.enableIPv6();

    this->setConnectTime(millis());
}

// --- Maintenance ---

// Pump the captive-portal DNS every tick, and on the
// WIFI_FALLBACK_STA_INTERVAL_MS cadence attempt to rejoin a saved network in
// case credentials were configured while running as an AP.
void WifiService::maintainAp() {
    this->captiveDns.processNextRequest();

    // Only retry STA when preferred mode is STA (we are in fallback AP).
    if (this->prefMode != WifiMode::Sta) {
        return;
    }

    const uint32_t now = millis();
    if (!this->isRecoveryElapsed(now)) {
        return;
    }

    this->setRecoveryTime(now);
    this->switchMode();
}

// Per-tick STA upkeep, evaluated in priority order:
//   1. Connected       — clear pending timers (addresses refresh via events).
//   2. Awaiting connect — fall back to AP once WIFI_CONNECT_TIMEOUT_MS elapses.
//   3. Link lost        — reopen AP after WIFI_FALLBACK_AP_TIMEOUT_MS,
//                         retry WiFi.begin() every WIFI_RECONNECT_INTERVAL_MS.
void WifiService::maintainSta() {
    if (WiFi.status() == WL_CONNECTED) {
        if (this->isConnecting() || this->isLinkDown()) {
            this->resetTimers();
        }
        return;
    }

    const uint32_t now = millis();

    if (this->isConnecting()) {
        if (this->isConnectTimedOut(now)) {
            this->startApMode();
        }
        return;
    }

    if (!this->isLinkDown()) {
        this->setLinkDownTime(now);
    }

    if (this->isLinkDownTimedOut(now)) {
        this->startApMode();
    } else if (this->isReconnectElapsed(now)) {
        this->setReconnectTime(now);
        WiFi.begin(this->staSsid.c_str(), this->staPassword.c_str());
    }
}

// --- Status ---

WifiMode WifiService::getActiveMode() const {
    switch (WiFi.getMode()) {
    case WIFI_MODE_AP:
        return WifiMode::Ap;
    case WIFI_MODE_STA:
        return WifiMode::Sta;
    default:
        return WifiMode::Off;
    }
}

int WifiService::getRssi() const {
    return WiFi.RSSI();
}

// --- Lifecycle ---

void WifiService::begin() {
    const WifiConfig cfg = WifiConfig::read();

    this->prefMode = cfg.prefMode;
    this->apSsid = cfg.apSsid;
    this->apPassword = cfg.apPassword;
    this->staSsid = cfg.staSsid;
    this->staPassword = cfg.staPassword;

    WiFi.persistent(false);
    WiFi.setHostname(this->device.getName().c_str());

    this->wifiEventId = WiFi.onEvent([this](arduino_event_id_t event, arduino_event_info_t) {
        switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
            this->networkChanged = true;
            break;
        default:
            break;
        }
    });

    this->switchMode();
}

void WifiService::end() {
    this->resetMode();
    WiFi.removeEvent(this->wifiEventId);
    this->networkChanged = false;

    this->prefMode = WifiMode::Off;
    this->apSsid.clear();
    this->apPassword.clear();
    this->staSsid.clear();
    this->staPassword.clear();

    this->macAddr = UNASSIGNED_MAC;
    this->ipv4Addr = IPAddress(IPv4).toString();
    this->ipv6Addr = IPAddress(IPv6).toString();
}

void WifiService::restart() {
    this->end();
    this->begin();
}

void WifiService::loop() {
    if (this->networkChanged) {
        this->networkChanged = false;
        this->updateNetworkAddrs();
    }

    switch (this->getActiveMode()) {
    case WifiMode::Off:
        break;
    case WifiMode::Ap:
        this->maintainAp();
        break;
    case WifiMode::Sta:
        this->maintainSta();
        break;
    }
}
