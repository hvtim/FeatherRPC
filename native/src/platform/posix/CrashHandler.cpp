#include "CrashHandler.h"

#include "core/ConfigPaths.h"
#include "core/CrashRing.h"
#include "core/Version.h"

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>

// A signal handler can interrupt malloc() mid-operation while it holds its
// own lock, so malloc/new (directly or via std::string/snprintf/iostream)
// risks deadlock here. Restricted to POSIX async-signal-safe functions
// only (signal-safety(7)): write(), uname(), time(), backtrace(), and
// backtrace_symbols_fd() specifically (not backtrace_symbols(), which
// mallocs). No snprintf/strftime - timestamps are raw epoch seconds.
// All formatting is hand-rolled onto fixed stack buffers.

namespace platform_posix {

namespace {

int g_crashFd = -1;

// Not a mutex (same reentrancy risk as malloc) - a lock-free "first one
// in wins" gate against two threads interleaving into the same file.
std::atomic_flag g_handling = ATOMIC_FLAG_INIT;

// A dedicated signal stack is required for SIGSEGV specifically - a
// stack-overflow fault leaves no usable space on the thread's own stack.
alignas(16) char g_altStackBuf[65536];

void WriteRaw(const char* text, size_t len) {
    if (g_crashFd < 0 || len == 0) {
        return;
    }
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(g_crashFd, text + written, len - written);
        if (n <= 0) {
            break;
        }
        written += static_cast<size_t>(n);
    }
}

void WriteRaw(const char* text) {
    WriteRaw(text, std::strlen(text));
}

void WriteHex(uintptr_t value) {
    char digits[2 * sizeof(uintptr_t)];
    int count = 0;
    if (value == 0) {
        digits[count++] = '0';
    } else {
        while (value != 0 && count < static_cast<int>(sizeof(digits))) {
            unsigned nibble = value & 0xF;
            digits[count++] = static_cast<char>(nibble < 10 ? ('0' + nibble) : ('a' + nibble - 10));
            value >>= 4;
        }
    }
    WriteRaw("0x");
    char buf[2 * sizeof(uintptr_t)];
    for (int i = 0; i < count; ++i) {
        buf[i] = digits[count - 1 - i];
    }
    WriteRaw(buf, static_cast<size_t>(count));
}

void WriteDec(long value) {
    if (value < 0) {
        WriteRaw("-");
        // LONG_MIN's magnitude doesn't fit in a positive long.
        unsigned long mag = static_cast<unsigned long>(-(value + 1)) + 1UL;
        value = 0;
        char digits[20];
        int count = 0;
        if (mag == 0) {
            digits[count++] = '0';
        } else {
            while (mag != 0 && count < static_cast<int>(sizeof(digits))) {
                digits[count++] = static_cast<char>('0' + (mag % 10));
                mag /= 10;
            }
        }
        char buf[20];
        for (int i = 0; i < count; ++i) {
            buf[i] = digits[count - 1 - i];
        }
        WriteRaw(buf, static_cast<size_t>(count));
        return;
    }
    unsigned long v = static_cast<unsigned long>(value);
    char digits[20];
    int count = 0;
    if (v == 0) {
        digits[count++] = '0';
    } else {
        while (v != 0 && count < static_cast<int>(sizeof(digits))) {
            digits[count++] = static_cast<char>('0' + (v % 10));
            v /= 10;
        }
    }
    char buf[20];
    for (int i = 0; i < count; ++i) {
        buf[i] = digits[count - 1 - i];
    }
    WriteRaw(buf, static_cast<size_t>(count));
}

const char* SignalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGFPE: return "SIGFPE";
        case SIGILL: return "SIGILL";
        case SIGBUS: return "SIGBUS";
        default: return "unknown signal";
    }
}

void WriteRecentLog() {
    uint32_t total = core::g_crashRing.writeIndex.load(std::memory_order_relaxed);
    uint32_t startSlot = (total >= core::kCrashRingSlots) ? (total % core::kCrashRingSlots) : 0;
    uint32_t slotsToRead = (total >= core::kCrashRingSlots) ? core::kCrashRingSlots : total;
    for (uint32_t i = 0; i < slotsToRead; ++i) {
        uint32_t slot = (startSlot + i) % core::kCrashRingSlots;
        const char* line = core::g_crashRing.slots[slot];
        if (line[0] != '\0') {
            WriteRaw(line);
            WriteRaw("\n");
        }
    }
}

void CrashHandlerImpl(int sig, siginfo_t* info, void* /*ucontext*/) {
    if (g_handling.test_and_set(std::memory_order_acquire)) {
        // Another thread is already writing a report - don't interleave.
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }

    if (g_crashFd >= 0) {
        lseek(g_crashFd, 0, SEEK_SET);

        WriteRaw("FeatherRPC crash report\n");
        WriteRaw("Time (epoch seconds): ");
        WriteDec(static_cast<long>(time(nullptr)));
        WriteRaw("\n");

        WriteRaw("Version: ");
        WriteRaw(core::kBuildString);
        WriteRaw("\n");

        struct utsname uts;
        if (uname(&uts) == 0) {
            WriteRaw("OS: ");
            WriteRaw(uts.sysname);
            WriteRaw(" ");
            WriteRaw(uts.release);
            WriteRaw(" ");
            WriteRaw(uts.machine);
            WriteRaw("\n");
        }

        WriteRaw("Signal: ");
        WriteRaw(SignalName(sig));
        WriteRaw(" (");
        WriteDec(sig);
        WriteRaw(")\n");

        if (info != nullptr) {
            WriteRaw("Fault address: ");
            WriteHex(reinterpret_cast<uintptr_t>(info->si_addr));
            WriteRaw("\n");
        }

        WriteRaw("\n-- Backtrace (symbolize offline against the matching unstripped binary; see docs/Releasing.md) --\n");
        void* frames[32];
        int frameCount = backtrace(frames, 32);
        backtrace_symbols_fd(frames, frameCount, g_crashFd);

        WriteRaw("\n-- Recent log lines (best-effort; a line may be torn if the crash landed mid-write) --\n");
        WriteRecentLog();

        // Truncate to this report's length - opened without O_TRUNC so a
        // previous session's crash survives until read.
        ftruncate(g_crashFd, lseek(g_crashFd, 0, SEEK_CUR));
        fsync(g_crashFd);
    }

    // Re-raise with default disposition (not _exit()) so the OS's own
    // crash capture (coredumpctl/DiagnosticReports) still fires too.
    signal(sig, SIG_DFL);
    raise(sig);
}

}  // namespace

void InstallCrashHandler() {
    // No O_TRUNC: don't discard a previous session's unread crash report.
    g_crashFd = open(core::GetCrashFilePath().c_str(), O_WRONLY | O_CREAT, 0644);

    stack_t altStack;
    altStack.ss_sp = g_altStackBuf;
    altStack.ss_size = sizeof(g_altStackBuf);
    altStack.ss_flags = 0;
    sigaltstack(&altStack, nullptr);

    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_sigaction = &CrashHandlerImpl;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);

    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
        sigaction(sig, &action, nullptr);
    }
}

}  // namespace platform_posix
