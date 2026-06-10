// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Runtime: top-level composition root.
//
// Owns every service by value and wires their dependencies via constructor
// references (e.g. WifiService needs Device; SshService needs SerialService).
// Member declaration order is therefore significant: it sets both C++
// construction order and the begin() boot order, and dependencies must be
// declared before their dependents.

#pragma once

#include "device.h"
#include "led.h"
#include "ssh.h"
#include "serial.h"
#include "web.h"
#include "wifi.h"

// ============================================================
//                           Runtime
// ============================================================

// Top-level composition root: owns all services and drives boot order.
class Runtime {
  private:
    // --- Boot colors palette ---

    static constexpr Color BOOT_COLOR_DEVICE = Color::rgb(255, 0, 0);   // Device — red
    static constexpr Color BOOT_COLOR_SERIAL = Color::rgb(255, 120, 0); // Serial — orange
    static constexpr Color BOOT_COLOR_WIFI = Color::rgb(0, 255, 0);     // Wi-Fi — green
    static constexpr Color BOOT_COLOR_SSH = Color::rgb(255, 255, 0);    // SSH — yellow
    static constexpr Color BOOT_COLOR_WEB = Color::rgb(0, 0, 255);      // Web — blue
    static constexpr Color BOOT_COLOR_DONE = Color::rgb(255, 255, 255); // ready — white

    // --- Services (declaration order = init order) ---

    Device device;
    Led led;
    WifiService wifi{device};

    SerialService serial{led};
    SshService ssh{serial};

    WebService web{device, serial, ssh, wifi};

  public:
    // --- Lifecycle ---

    // Boot all services in dependency order.
    void begin();

    // Periodic housekeeping; call every loop iteration.
    void loop();
};
