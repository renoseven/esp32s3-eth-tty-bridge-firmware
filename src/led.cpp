// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Led implementation.
//
// Display model: one steady color (boot/status) plus up to OVERLAY_MAX
// short-lived overlays (TX/RX flashes). At any instant the pin shows an active
// overlay or the steady color. Multiple concurrent overlays are time-multiplexed
// by a periodic esp_timer (LED_ROTATE_INTERVAL_MS) that also expires finished
// ones. The timer only runs while overlays are pending, so a purely steady LED
// costs nothing.

#include "led.h"

#include <esp32-hal-rgb-led.h>
#include <esp32-hal.h>

// ============================================================
//                             LED
// ============================================================

// --- Helpers ---

// Scale each channel by the packed brightness byte so one brightness control
// applies uniformly. pin == 0 means "not bound yet".
void Led::write(Color color) const {
    if (this->pin == 0) {
        return;
    }

    if (color.isOff()) {
        rgbLedWrite(this->pin, 0, 0, 0);
        return;
    }

    const uint8_t brightness = color.brightness();
    const uint8_t red = static_cast<uint8_t>(static_cast<uint16_t>(color.red()) * brightness / 0xFF);
    const uint8_t green = static_cast<uint8_t>(static_cast<uint16_t>(color.green()) * brightness / 0xFF);
    const uint8_t blue = static_cast<uint8_t>(static_cast<uint16_t>(color.blue()) * brightness / 0xFF);

    rgbLedWrite(this->pin, red, green, blue);
}

// Extending from max(until, now) lets repeated flashes of the same color
// lengthen the overlay rather than restart it.
void Led::extend(uint32_t& until, uint32_t durationMs) {
    const uint32_t now = millis();
    const uint32_t base = (until > now) ? until : now;

    until = base + durationMs;
}

// Reuse a live slot of the same color, else take a free/expired slot, else evict
// the one expiring soonest. The deadline is left for the caller to set.
Led::Overlay& Led::select(Color color) {
    const uint32_t now = millis();

    for (Overlay& overlay : this->overlays) {
        if (overlay.color == color && overlay.until > now) {
            return overlay;
        }
    }

    for (Overlay& overlay : this->overlays) {
        if (overlay.color.isOff() || overlay.until <= now) {
            overlay.color = color;
            overlay.until = 0;
            return overlay;
        }
    }

    Overlay* oldest = &this->overlays[0];
    for (Overlay& overlay : this->overlays) {
        if (overlay.until < oldest->until) {
            oldest = &overlay;
        }
    }

    oldest->color = color;
    oldest->until = 0;

    return *oldest;
}

// Show an active overlay if any, otherwise the steady color (or off). When
// several overlays are active, rotateIdx picks which one is currently visible.
void Led::refresh() {
    const uint32_t now = millis();
    Color active[OVERLAY_MAX];
    size_t count = 0;

    for (const Overlay& overlay : this->overlays) {
        if (!overlay.color.isOff() && overlay.until > now) {
            active[count++] = overlay.color;
        }
    }

    if (count == 0) {
        if (!this->steady.isOff()) {
            this->write(this->steady);
        } else {
            this->write(Led::COLOR_OFF);
        }
        return;
    }

    if (count == 1) {
        this->write(active[0]);
        return;
    }

    this->write(active[this->rotateIdx % count]);
}

// Start the timer when overlays appear, stop it when the LED is steady-only, so
// a purely steady LED costs nothing.
void Led::schedule() {
    if (this->timer == nullptr) {
        return;
    }

    const uint32_t now = millis();
    bool active = false;

    for (const Overlay& overlay : this->overlays) {
        if (!overlay.color.isOff() && overlay.until > now) {
            active = true;
            break;
        }
    }

    if (!active) {
        esp_timer_stop(this->timer);
        return;
    }

    if (esp_timer_is_active(this->timer)) {
        return;
    }

    const uint64_t periodUs = static_cast<uint64_t>(Firmware::LED_ROTATE_INTERVAL_MS) * 1000;
    esp_timer_start_periodic(this->timer, periodUs);
}

// Drop expired overlays, advance the rotation cursor only when two or more
// remain visible, then repaint and re-arm/stop the timer.
void Led::onTimer(void* arg) {
    Led& self = *static_cast<Led*>(arg);

    const uint32_t now = millis();
    size_t active = 0;

    for (Overlay& overlay : self.overlays) {
        if (!overlay.color.isOff() && overlay.until <= now) {
            overlay.color = Led::COLOR_OFF;
            overlay.until = 0;
        } else if (!overlay.color.isOff()) {
            active++;
        }
    }

    if (active >= 2) {
        self.rotateIdx++;
    }

    self.refresh();
    self.schedule();
}

// --- Output ---

void Led::show(Color color) {
    this->steady = color;
    for (Overlay& overlay : this->overlays) {
        overlay.color = Led::COLOR_OFF;
        overlay.until = 0;
    }
    this->rotateIdx = 0;
    this->refresh();
    this->schedule();
}

void Led::flash(Color color, uint32_t durationMs) {
    if (color.isOff()) {
        return;
    }

    if (durationMs < Firmware::LED_FLASH_DURATION_MS) {
        durationMs = Firmware::LED_FLASH_DURATION_MS;
    }

    Overlay& overlay = this->select(color);
    this->extend(overlay.until, durationMs);
    this->refresh();
    this->schedule();
}

void Led::off() {
    this->steady = Led::COLOR_OFF;
    for (Overlay& overlay : this->overlays) {
        overlay.color = Led::COLOR_OFF;
        overlay.until = 0;
    }
    this->rotateIdx = 0;
    this->write(Led::COLOR_OFF);

    if (this->timer != nullptr) {
        esp_timer_stop(this->timer);
    }
}

// --- Lifecycle ---

void Led::begin(uint8_t pin) {
    if (this->timer != nullptr) {
        return;
    }

    const esp_timer_create_args_t args{
        .callback = &Led::onTimer,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "led",
        .skip_unhandled_events = true,
    };

    this->pin = pin;
    esp_timer_create(&args, &this->timer);
    this->off();
}

void Led::end() {
    if (this->timer != nullptr) {
        esp_timer_stop(this->timer);
        esp_timer_delete(this->timer);
        this->timer = nullptr;
    }

    this->off();
    this->pin = 0;
}
