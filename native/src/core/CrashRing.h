#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace core {

// Fixed-size, lock-free ring buffer of recent log lines - readable from a
// signal handler / SEH filter, where Log's own file I/O and mutex aren't
// safe to touch. No lock on read; a crash mid-write can tear one line.
constexpr size_t kCrashRingSlots = 64;
constexpr size_t kCrashRingSlotSize = 160; // includes null terminator

struct CrashRingBuffer {
    std::atomic<uint32_t> writeIndex{0};
    char slots[kCrashRingSlots][kCrashRingSlotSize]{};
};

extern CrashRingBuffer g_crashRing;

} // namespace core
