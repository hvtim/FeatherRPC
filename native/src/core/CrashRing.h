#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace core {

// Fixed-size, lock-free, no-dynamic-allocation ring buffer of recent log
// lines, so a crash handler running in a signal handler / SEH filter -
// where core::Log's own file I/O and mutex are both unsafe to touch - can
// still dump "what was happening right before this" via nothing but raw,
// bounded reads.
//
// Log::Write() is the only writer, and it writes in normal (non-signal-
// handler) context, so the write side doesn't need to be async-signal-safe
// itself. The crash handler is the only reader, and it reads this memory
// with no lock at all - safe because a plain read of already-allocated
// static storage can't fault or corrupt anything, even mid-write. The one
// accepted tradeoff: if the crash happens mid-write to the slot the crash
// handler ends up reading, that one line may be torn (a mix of old and new
// content). Best-effort diagnostics, not correctness-critical.
constexpr size_t kCrashRingSlots = 64;
constexpr size_t kCrashRingSlotSize = 160; // includes the null terminator; longer lines are truncated

struct CrashRingBuffer {
    std::atomic<uint32_t> writeIndex{0};
    char slots[kCrashRingSlots][kCrashRingSlotSize]{};
};

// Static storage duration, zero-initialized before any constructor runs -
// safe to write to from Log::Write() as early as process startup, and
// safe for a crash handler to read even if the crash happens before
// main()'s normal initialization has finished.
extern CrashRingBuffer g_crashRing;

} // namespace core
