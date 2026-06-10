// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Device implementation: NVS-backed device config persistence and SDK-dependent
// helpers (default device name derivation, reboot, config reload).

#include "device.h"

#include <Preferences.h>
#include <mutex>

#include "firmware.h"

namespace {
    std::mutex deviceNvsMutex;

    constexpr const char* NVS_NAMESPACE = "device";
    constexpr const char* NVS_KEY_DEVICE_NAME = "name";

    // Default device name: base name + device ID.
    String defaultDeviceName() {
        char buf[Firmware::LIMIT_DEVICE_NAME_MAX + 1];

        snprintf(buf, sizeof(buf), "%s%04X", Firmware::DEFAULT_DEVICE_NAME_PREFIX, Device::getDeviceId());

        return String(buf);
    }
} // namespace

// ============================================================
//                        Device Config
// ============================================================

DeviceConfig DeviceConfig::read() {
    std::lock_guard<std::mutex> lock(deviceNvsMutex);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    DeviceConfig cfg{
        .name = prefs.getString(NVS_KEY_DEVICE_NAME, defaultDeviceName()),
    };
    prefs.end();

    return cfg;
}

void DeviceConfig::write() const {
    std::lock_guard<std::mutex> lock(deviceNvsMutex);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY_DEVICE_NAME, this->name);
    prefs.end();
}

void DeviceConfig::clear() {
    std::lock_guard<std::mutex> lock(deviceNvsMutex);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
}

// ============================================================
//                           Device
// ============================================================

// --- Control ---

void Device::reboot() {
    ESP.restart();
}

// --- Lifecycle ---

void Device::reload() {
    const DeviceConfig cfg = DeviceConfig::read();

    this->name = cfg.name;
}
