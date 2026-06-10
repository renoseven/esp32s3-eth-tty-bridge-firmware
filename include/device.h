// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Device: identity and system facts.
//
// Holds the configured device name (loaded from NVS) and exposes read-only
// system information consumed by the status API — firmware version, heap usage,
// and uptime — plus a reboot helper. Trivial getters are defined inline.

#pragma once

#include <Arduino.h>

#include "firmware.h"

// ============================================================
//                        Device Config
// ============================================================

// Persisted device identity loaded and stored through NVS.
struct DeviceConfig {
    String name;

    // Read config from NVS (falls back to defaults for absent keys).
    static DeviceConfig read();

    // Write this config to NVS.
    void write() const;

    // Erase all keys in the device NVS namespace.
    static void clear();
};

// ============================================================
//                           Device
// ============================================================

// Device name plus read-only system facts and a reboot helper.
class Device {
  private:
    // --- Configuration ---

    String name; // Device name (used as network hostname).

  public:
    // --- Status ---

    // Return a short unique board identifier.
    static uint16_t getDeviceId() {
        return static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFFu);
    }

    // Return the device name.
    const String& getName() const {
        return this->name;
    }

    // Return the firmware version string.
    static const char* getFirmwareVersion() {
        return Firmware::VERSION;
    }

    // Return free heap memory in bytes.
    static uint32_t getFreeMemory() {
        return ESP.getFreeHeap();
    }

    // Return total heap memory in bytes.
    static uint32_t getTotalMemory() {
        return ESP.getHeapSize();
    }

    // Return uptime in seconds.
    static uint32_t getUptime() {
        return millis() / 1000;
    }

    // --- Control ---

    // Reboot the device.
    static void reboot();

    // --- Lifecycle ---

    // Load device configuration from NVS (boot and after config changes).
    void reload();
};
