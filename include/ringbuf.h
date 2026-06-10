// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Lock-free single-producer/single-consumer (SPSC) byte ring buffer.
//
// Correctness rests on three invariants:
//   - Exactly one producer thread and one consumer thread. The producer only
//     mutates head; the consumer only mutates tail. Violating this breaks the
//     buffer (it is NOT MPMC-safe).
//   - head/tail are free-running counters (never wrapped); SIZE being a power of
//     two lets `& mask` map them to slots, and unsigned overflow of the
//     difference still yields the correct fill level.
//   - Release/acquire pairing publishes data before the index that exposes it:
//     each side commits its own counter with release and reads the other's with
//     acquire, so committed bytes are visible before the matching cursor moves.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// ============================================================
//                           RingBuf
// ============================================================

// SIZE must be a power of two; usable capacity is SIZE-1
// (one slot is reserved to distinguish full from empty).
//
// Zero-copy usage per side:
// *  Producer: n = writeContiguous(); fill writeBuf()[0..n); writeCommit(n).
// *  Consumer: n = readContiguous();  drain readBuf()[0..n); readCommit(n).
// "Contiguous" returns the span up to the next wrap, so callers copy without
// straddling the end of the backing array.
template <size_t SIZE> class RingBuf {
    static_assert((SIZE & (SIZE - 1)) == 0, "RingBuf capacity must be a power of two");

    // SIZE is a power of two, so head & mask and tail & mask yield indices in [0, SIZE-1].
    static constexpr size_t mask = SIZE - 1;

  private:
    // --- Runtime state ---

    // head and tail are aligned onto separate cache lines so the producer's and
    // consumer's hot writes do not false-share.
    uint8_t buffer[SIZE];                     // Backing storage.
    alignas(32) std::atomic<size_t> head = 0; // Producer write counter.
    alignas(32) std::atomic<size_t> tail = 0; // Consumer read counter.

  public:
    // --- Capacity ---

    // Return bytes ready for the consumer to read.
    size_t readable() const {
        const size_t tail = this->tail.load(std::memory_order_relaxed);
        const size_t head = this->head.load(std::memory_order_acquire);

        return head - tail;
    }

    // Return bytes the producer can still write.
    size_t writable() const {
        const size_t head = this->head.load(std::memory_order_relaxed);
        const size_t tail = this->tail.load(std::memory_order_acquire);

        return (SIZE - 1) - (head - tail);
    }

    // --- Read ---

    // Return max contiguous bytes readable without wrapping.
    size_t readContiguous() const {
        const size_t tail = this->tail.load(std::memory_order_relaxed);
        const size_t head = this->head.load(std::memory_order_acquire);

        const size_t len = head - tail;
        if (len == 0) {
            return 0;
        }

        const size_t index = tail & mask;
        const size_t wrap = SIZE - index;

        return len < wrap ? len : wrap;
    }

    // Return the consumer read region; valid for at most readContiguous() bytes.
    const uint8_t* readBuf() const {
        return &this->buffer[this->tail.load(std::memory_order_relaxed) & mask];
    }

    // Advance the read cursor after consuming length bytes from readBuf().
    void readCommit(size_t len) {
        this->tail.fetch_add(len, std::memory_order_release);
    }

    // --- Write ---

    // Return max contiguous bytes writable without wrapping.
    size_t writeContiguous() const {
        const size_t head = this->head.load(std::memory_order_relaxed);
        const size_t tail = this->tail.load(std::memory_order_acquire);

        const size_t len = (SIZE - 1) - (head - tail);
        if (len == 0) {
            return 0;
        }

        const size_t index = head & mask;
        const size_t wrap = SIZE - index;

        return len < wrap ? len : wrap;
    }

    // Return the producer write region; valid for at most writeContiguous() bytes.
    uint8_t* writeBuf() {
        return &this->buffer[this->head.load(std::memory_order_relaxed) & mask];
    }

    // Advance the write cursor after copying length bytes into writeBuf().
    void writeCommit(size_t len) {
        this->head.fetch_add(len, std::memory_order_release);
    }
};
