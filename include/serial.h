// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Serial Service: bridges the USB-CDC serial port to the network side.
//
// Exposes two lock-free SPSC ring buffers (usbRx, usbTx) and runs a dedicated
// task that copies bytes between USB-CDC and those buffers. The network task
// (SshService) is the peer endpoint; ownership of each buffer is split so the
// SPSC contract holds. Both tasks poll at 1ms (USB Full Speed frame interval).

#pragma once

#include <USB.h>
#include <USBCDC.h>

#include "firmware.h"
#include "led.h"
#include "ringbuf.h"

// ============================================================
//                       Serial Service
// ============================================================

// USB-CDC <-> ring-buffer bridge: a dedicated task copies bytes both ways.
class SerialService {
  private:
    static constexpr Color COLOR_TX = Color::rgb(0, 200, 0);   // TX — green
    static constexpr Color COLOR_RX = Color::rgb(255, 200, 0); // RX — yellow

    // --- Runtime state ---

    Led& led;                          // LED service for feedback.
    USBCDC cdc;                        // USB-CDC interface.
    TaskHandle_t taskHandle = nullptr; // USB bridge task handle.

    // USB bridge task loop: copy bytes between USB-CDC and the ring buffers.
    static void task(void* arg);

  public:
    explicit SerialService(Led& led) : led(led) {}

    // --- Buffers ---

    // Direction fixes producer/consumer roles (the basis of the SPSC contract):
    //   usbRx: serial task writes, network task reads.
    //   usbTx: network task writes, serial task reads.
    RingBuf<Firmware::SERIAL_BUF_SIZE> usbRx; // USB -> network
    RingBuf<Firmware::SERIAL_BUF_SIZE> usbTx; // network -> USB

    // --- Lifecycle ---

    // Start USB-CDC and spawn the bridge task. No matching teardown: the USB
    // serial port is meant to stay up for the device's lifetime.
    void begin();
};
