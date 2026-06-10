// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SerialBridge — ESP32-S3 USB-CDC serial port over SSH.
//
// Plug a USB-CDC target into the board's host port and reach its serial port
// over the network via SSH, with a built-in web UI for configuration.
//
// Entry point — Arduino requires setup()/loop() in the sketch. All firmware
// logic lives in include/ and src/; this file only holds the global Runtime
// composition root and forwards setup() -> begin(), loop() -> loop().
//
//   Bridge  USB host CDC; 8 KiB ring buffers each way; dedicated task on core 1.
//   SSH     :22  password, public key, optional no-auth; Ed25519 host key in NVS.
//   Web     :80  config, status, i18n (EN / 中文), reboot, factory reset.
//   Wi-Fi   STA when configured; captive-portal AP fallback (SSID = hostname).
//   LED     GPIO 21 RGB — boot: device/serial/Wi-Fi/SSH/web/ready; traffic: TX/RX.
//   Config  device, Wi-Fi, SSH in NVS; web save restarts only the affected service.
//
// Libraries: ESPAsyncWebServer, AsyncTCP, ArduinoJson, LibSSH-ESP32 (sketch.yaml).
// UTF-8 sources. Build injects git short HEAD as FW_VERSION_ID (build_opt.h).

#include "runtime.h"

Runtime g_runtime;

void setup() {
    g_runtime.begin();
}

void loop() {
    g_runtime.loop();
}
