// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Serial Service implementation.
//
// A single bridge task copies bytes between the USB-CDC port and the two SPSC
// ring buffers: CDC -> usbRx (toward the network/SSH side) and usbTx -> CDC
// (from the network side). The task is the consumer of usbTx and producer of
// usbRx; the SSH task is the matching counterpart, which keeps each ring buffer
// strictly single-producer/single-consumer and therefore lock-free.
//
// The task polls at BRIDGE_POLL_INTERVAL_MS (1ms), matching the USB Full Speed
// frame interval so CDC RX latency is bounded by the physical link. RX/TX LED
// flashes provide visible traffic feedback.

#include "serial.h"

// ============================================================
//                       Serial Service
// ============================================================

// --- Task ---

// Each pass drains both directions as far as the buffers allow, then sleeps when
// idle. Both directions are serviced every iteration so neither starves the other.
void SerialService::task(void* arg) {
    SerialService& svc = *static_cast<SerialService*>(arg);

    for (;;) {
        bool rxWork = false;
        bool txWork = false;

        // CDC -> usbRx: move host input toward the network side.
        for (;;) {
            if (svc.cdc.available() <= 0)
                break;

            const size_t chunk = svc.usbRx.writeContiguous();
            if (chunk == 0)
                break;

            const int r = svc.cdc.read(svc.usbRx.writeBuf(), chunk);
            if (r <= 0)
                break;

            svc.usbRx.writeCommit(static_cast<size_t>(r));
            rxWork = true;
        }

        // usbTx -> CDC: flush network output to the host.
        for (;;) {
            const size_t chunk = svc.usbTx.readContiguous();
            if (chunk == 0)
                break;

            const size_t w = svc.cdc.write(svc.usbTx.readBuf(), chunk);
            if (w == 0)
                break;

            svc.usbTx.readCommit(w);
            txWork = true;
        }

        if (rxWork || txWork) {
            if (rxWork)
                svc.led.flash(SerialService::COLOR_RX);
            if (txWork)
                svc.led.flash(SerialService::COLOR_TX);
        } else {
            vTaskDelay(pdMS_TO_TICKS(Firmware::BRIDGE_POLL_INTERVAL_MS));
        }
    }
}

// --- Lifecycle ---

void SerialService::begin() {
    this->cdc.begin();
    USB.begin();
    xTaskCreatePinnedToCore(SerialService::task, "serial", Firmware::SERIAL_TASK_STACK, this,
                            Firmware::SERIAL_TASK_PRIO, &this->taskHandle, Firmware::SERIAL_TASK_CORE);
}
