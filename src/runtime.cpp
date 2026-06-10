// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Runtime implementation: boots services in dependency order and runs periodic
// housekeeping. The LED is brought up first so each boot stage can paint a
// distinct color, turning the status LED into a progress/fault indicator: the
// color shown when a hang occurs identifies the stage that stalled.

#include "runtime.h"

// ============================================================
//                           Runtime
// ============================================================

// --- Lifecycle ---

void Runtime::begin() {
    this->led.begin(Firmware::LED_PIN);

    this->led.show(Runtime::BOOT_COLOR_DEVICE);
    this->device.reload();

    this->led.show(Runtime::BOOT_COLOR_SERIAL);
    this->serial.begin();

    this->led.show(Runtime::BOOT_COLOR_WIFI);
    this->wifi.begin();

    this->led.show(Runtime::BOOT_COLOR_SSH);
    this->ssh.begin();

    this->led.show(Runtime::BOOT_COLOR_WEB);
    this->web.begin();

    this->led.show(Runtime::BOOT_COLOR_DONE);
}

// Only WiFi needs servicing on the main loop; Serial and SSH run on their own tasks
// and the web server is driven by AsyncTCP. yield() lets lower-priority
// background work (idle task, housekeeping) run.
void Runtime::loop() {
    this->wifi.loop();
    yield();
}
