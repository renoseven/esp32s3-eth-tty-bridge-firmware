// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Wi-Fi service: owns the device's network connectivity.
//
// Manages a single active radio mode (Off / STA / configuration-AP), runs the
// captive portal in AP mode, and performs automatic failover between joining a
// saved network and falling back to the configuration AP. Loaded credentials
// come from NVS; the WebService triggers restart() after config changes.
//
// Not thread-safe: drive begin()/loop()/restart() from a single task.

#pragma once

#include <DNSServer.h>

#include "firmware.h"
#include <WString.h>

#include <cstdint>

class Device;

// ============================================================
//                         Wi-Fi Mode
// ============================================================

// Active radio role.
enum class WifiMode : uint8_t {
    Off = 0, // WIFI_MODE_NULL — Wi-Fi disabled.
    Sta = 1, // WIFI_MODE_STA — connect using saved SSID.
    Ap = 2,  // WIFI_MODE_AP — configuration softAP.
};

// ============================================================
//                        Wi-Fi Config
// ============================================================

// Persisted Wi-Fi configuration.
struct WifiConfig {
    WifiMode prefMode;  // Preferred radio mode.
    String apSsid;      // AP SSID.
    String apPassword;  // AP passphrase.
    String staSsid;     // STA SSID.
    String staPassword; // STA passphrase.

    // Read config from NVS (falls back to defaults for absent keys).
    static WifiConfig read();

    // Write this config to NVS.
    void write() const;

    // Erase all keys in the wifi NVS namespace.
    static void clear();
};

// ============================================================
//                       Wi-Fi Service
// ============================================================

// Wi-Fi connectivity: STA with automatic configuration-AP fallback.
class WifiService {
  private:
    // --- Constants ---

    static constexpr const char* UNASSIGNED_MAC = "00:00:00:00:00:00";

    // --- Configuration ---

    WifiMode prefMode;  // Preferred radio mode.
    String apSsid;      // AP SSID.
    String apPassword;  // AP passphrase.
    String staSsid;     // STA SSID.
    String staPassword; // STA passphrase.

    // --- Network state ---

    String macAddr;  // MAC address.
    String ipv4Addr; // IPv4 address.
    String ipv6Addr; // IPv6 address.

    // --- Runtime state ---

    Device& device;       // Device information.
    DNSServer captiveDns; // DNS server for captive portal.

    size_t wifiEventId = 0;               // Wi-Fi event handle.
    volatile bool networkChanged = false; // Set by Wi-Fi event callbacks.

    uint32_t connectTime = 0;   // Nonzero while waiting for first connection.
    uint32_t reconnectTime = 0; // Last reconnect attempt while link is down.
    uint32_t linkDownTime = 0;  // Anchor for recovery timeout after link loss.
    uint32_t recoveryTime = 0;  // Last recovery attempt from fallback.

    // --- Timer predicates ---
    //
    // Four monotonic timestamps drive the STA ↔ AP failover state machine:
    //   connectTime   – set when STA begins associating; cleared on success.
    //   reconnectTime – rate-limits STA reconnect attempts while link is down.
    //   linkDownTime  – anchors the "give up STA, fall back to AP" deadline.
    //   recoveryTime  – rate-limits "try STA again" probes while in fallback AP.
    //
    // A zero timestamp means the corresponding phase is inactive.

    // True while the initial STA association is in progress.
    bool isConnecting() const {
        return this->connectTime != 0;
    }

    // True after the STA link has dropped and we are tracking recovery.
    bool isLinkDown() const {
        return this->linkDownTime != 0;
    }

    // Initial STA association exceeded WIFI_CONNECT_TIMEOUT_MS.
    bool isConnectTimedOut(uint32_t now) const {
        return (now - this->connectTime) >= Firmware::WIFI_CONNECT_TIMEOUT_MS;
    }

    // STA link has been down longer than WIFI_FALLBACK_AP_TIMEOUT_MS.
    bool isLinkDownTimedOut(uint32_t now) const {
        return (now - this->linkDownTime) >= Firmware::WIFI_FALLBACK_AP_TIMEOUT_MS;
    }

    // Enough time has passed to attempt another STA reconnect.
    bool isReconnectElapsed(uint32_t now) const {
        return (now - this->reconnectTime) >= Firmware::WIFI_RECONNECT_INTERVAL_MS;
    }

    // Enough time has passed to probe STA recovery from fallback AP.
    bool isRecoveryElapsed(uint32_t now) const {
        return (now - this->recoveryTime) >= Firmware::WIFI_FALLBACK_STA_INTERVAL_MS;
    }

    // --- Timer setters ---

    // Mark the start of a STA association attempt.
    void setConnectTime(uint32_t now) {
        this->connectTime = now;
    }

    // Record the last STA reconnect attempt while link is down.
    void setReconnectTime(uint32_t now) {
        this->reconnectTime = now;
    }

    // Anchor the fallback-to-AP deadline after a link loss.
    void setLinkDownTime(uint32_t now) {
        this->linkDownTime = now;
    }

    // Record the last STA recovery probe from fallback AP.
    void setRecoveryTime(uint32_t now) {
        this->recoveryTime = now;
    }

    // Clear all timestamps, returning the state machine to idle.
    void resetTimers() {
        this->connectTime = 0;
        this->reconnectTime = 0;
        this->linkDownTime = 0;
        this->recoveryTime = 0;
    }

    // --- Mode management ---

    // Power the radio off and clear per-mode state; run before entering a mode.
    void resetMode();

    // Enter the mode that matches the saved config (STA if an SSID is set, else
    // configuration AP). No-op when already in that mode.
    void switchMode();

    // Start configuration softAP and captive portal.
    void startApMode();

    // Start joining the configured network.
    void startStaMode();

    // --- Maintenance ---

    // Update the cached MAC/IPv4/IPv6 status fields to match the current mode.
    void updateNetworkAddrs();

    // AP-mode upkeep: keep the captive portal responsive and periodically retry
    // joining a saved network.
    void maintainAp();

    // STA-mode upkeep: confirm the link, time out the first connect to AP, and
    // reconnect or fall back to AP while the link is down.
    void maintainSta();

  public:
    explicit WifiService(Device& device) : device(device) {}

    // --- Status ---

    // Return the preferred radio mode (from config).
    WifiMode getPrefMode() const {
        return this->prefMode;
    }

    // Return the actual radio mode (from hardware).
    WifiMode getActiveMode() const;

    // Return STA RSSI in dBm (0 when not in STA mode).
    int getRssi() const;

    // Return the configured AP SSID.
    const String& getApSsid() const {
        return this->apSsid;
    }

    // Return the configured STA SSID.
    const String& getStaSsid() const {
        return this->staSsid;
    }

    // Return the active interface MAC address.
    const String& getMacAddr() const {
        return this->macAddr;
    }

    // Return the active interface IPv4 address.
    const String& getIpv4Addr() const {
        return this->ipv4Addr;
    }

    // Return the active interface IPv6 address.
    const String& getIpv6Addr() const {
        return this->ipv6Addr;
    }

    // --- Lifecycle ---

    // Load credentials from NVS and enter the matching mode (STA or AP).
    void begin();

    // Power down the radio and clear all cached state and timers.
    void end();

    // Re-read NVS and re-enter the matching mode; call after config changes.
    void restart();

    // Advance the failover state machine; call every loop iteration.
    void loop();
};
