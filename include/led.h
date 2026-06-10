// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// RGB status LED.
//
// Provides a compact packed Color type and an Led service that drives the
// on-board addressable RGB pin. The service layers short timed "flash" overlays
// (e.g. TX/RX activity) on top of a steady boot/status color, multiplexing them
// with a periodic esp_timer when more than one is active.
// See led.cpp for the display/rotation model.

#pragma once

#include <esp_timer.h>

#include <cstddef>
#include <cstdint>

#include "firmware.h"

// ============================================================
//                            Color
// ============================================================

// Packed as 0xBBRRGGBB: brightness, then RGB.
struct Color {
    uint32_t value = 0;

    // Pack RGB and brightness into 0xBBRRGGBB.
    static constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = Firmware::LED_BRIGHTNESS) {
        uint32_t value = (static_cast<uint32_t>(brightness) << 24) | (static_cast<uint32_t>(r) << 16) |
                         (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        return Color{value};
    }

    // Return true when value is 0 (off).
    constexpr bool isOff() const {
        return this->value == 0;
    }

    // Return the packed brightness byte (BB).
    constexpr uint8_t brightness() const {
        return static_cast<uint8_t>((this->value >> 24) & 0xFF);
    }

    // Return the packed red byte (RR).
    constexpr uint8_t red() const {
        return static_cast<uint8_t>((this->value >> 16) & 0xFF);
    }

    // Return the packed green byte (GG).
    constexpr uint8_t green() const {
        return static_cast<uint8_t>((this->value >> 8) & 0xFF);
    }

    // Return the packed blue byte (low byte).
    constexpr uint8_t blue() const {
        return static_cast<uint8_t>(this->value & 0xFF);
    }

    // Compare packed color values.
    constexpr bool operator==(const Color& other) const {
        return this->value == other.value;
    }

    constexpr bool operator!=(const Color& other) const {
        return this->value != other.value;
    }
};

// ============================================================
//                             LED
// ============================================================

// RGB status LED: a steady base color with transient flash overlays, repainted
// by an esp_timer while overlays are active.
class Led {
  private:
    // --- Constants ---

    // Packed value 0; off state and empty overlay slots.
    static constexpr Color COLOR_OFF = Color();

    // Max concurrent timed overlays before evicting the oldest.
    static constexpr size_t OVERLAY_MAX = 2;

    // --- Types ---

    // Timed color overlay.
    struct Overlay {
        Color color = COLOR_OFF;
        uint32_t until = 0;
    };

    // --- Runtime state ---

    uint8_t pin = 0;                    // LED pin.
    Color steady = COLOR_OFF;           // Steady color.
    Overlay overlays[OVERLAY_MAX];      // Timed color overlays layered over steady.
    size_t rotateIdx = 0;               // Display index when multiple color overlays overlap.
    esp_timer_handle_t timer = nullptr; // LED timer handle.

    // Write color to the RGB pin (no-op until begin() binds a pin).
    void write(Color color) const;

    // Repaint the pin from the current steady color and active overlays.
    void refresh();

    // Start/stop the rotation timer to match whether overlays are pending.
    // Idempotent.
    void schedule();

    // Extend an overlay deadline by durationMs without shortening it.
    void extend(uint32_t& until, uint32_t durationMs);

    // Return the overlay slot to use for color, reusing/allocating/evicting as
    // needed. The caller sets the returned slot's deadline.
    Overlay& select(Color color);

    // Periodic timer callback: expire overlays, rotate, and repaint.
    static void onTimer(void* arg);

  public:
    // Show a solid boot/status color (clears active overlays).
    void show(Color color);

    // Flash color for durationMs (extends if the same color is already active).
    void flash(Color color, uint32_t durationMs = Firmware::LED_FLASH_DURATION_MS);

    // Turn off all output.
    void off();

    // --- Lifecycle ---

    // Bind pin and create timer; turn off output.
    void begin(uint8_t pin);

    // Stop timer, turn off output, and release resources.
    void end();
};
