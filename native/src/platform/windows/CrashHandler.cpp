#include "CrashHandler.h"

#include "core/ConfigPaths.h"
#include "core/CrashRing.h"
#include "core/Version.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

// Deliberately no DbgHelp/SymFromAddr anywhere in this file - a Release
// build ships no PDB, and symbol lookup itself risks further faulting on
// an already-corrupted process. Instead, each backtrace frame is resolved
// to only "module name + offset from module base", which is enough to
// symbolize later, offline, against the exact matching archived PDB (see
// docs/Releasing.md's Symbol archiving section). Formatting below is
// entirely hand-rolled (no snprintf/iostream) and writes go straight
// through WriteFile with no intermediate heap allocation - not because
// SEH filters are as restrictive as a POSIX signal handler, but because
// the CRT heap lock may be held by the very thread that just faulted, and
// any further heap operation (new/malloc, which snprintf/std::string can
// trigger) risks deadlocking instead of ever producing a report at all.

namespace platform_windows {

namespace {

HANDLE g_crashFile = INVALID_HANDLE_VALUE;

void WriteRaw(const char* text, size_t len) {
    if (g_crashFile == INVALID_HANDLE_VALUE || len == 0) {
        return;
    }
    DWORD written = 0;
    WriteFile(g_crashFile, text, static_cast<DWORD>(len), &written, nullptr);
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

void WriteDec(unsigned long value, int minWidth = 0) {
    char digits[10];
    int count = 0;
    if (value == 0) {
        digits[count++] = '0';
    } else {
        while (value != 0 && count < static_cast<int>(sizeof(digits))) {
            digits[count++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
    }
    char buf[10];
    int padded = count > minWidth ? count : minWidth;
    for (int i = 0; i < padded - count; ++i) {
        buf[i] = '0';
    }
    for (int i = 0; i < count; ++i) {
        buf[padded - count + i] = digits[count - 1 - i];
    }
    WriteRaw(buf, static_cast<size_t>(padded));
}

// Narrows a module's base file name (already stripped of its directory) to
// ASCII, best-effort - module file names are ASCII in every real-world
// case this matters for (nothing here needs to survive an exotic path).
void WriteModuleBaseName(const wchar_t* path) {
    const wchar_t* base = path;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'\\') {
            base = p + 1;
        }
    }
    char narrow[MAX_PATH];
    size_t i = 0;
    for (; base[i] != 0 && i < sizeof(narrow) - 1; ++i) {
        narrow[i] = (base[i] < 128) ? static_cast<char>(base[i]) : '?';
    }
    narrow[i] = '\0';
    WriteRaw(narrow);
}

// The first 1-2 frames are always this function's own call into
// CaptureStackBackTrace, and several frames after that are the OS's own
// exception-dispatch machinery (ntdll!KiUserExceptionDispatcher and
// friends) - not a bug, just how a top-level SEH filter necessarily sees
// the stack (the OS calls the filter as a nested call while the actual
// faulting frame is still live further down). Confirmed against a real
// symbolicated build: the genuine fault site reliably shows up a handful
// of frames past those - when reading a real report, skip past the
// CrashHandler.cpp/ntdll/KERNELBASE frames at the top to find it.
void WriteBacktrace() {
    void* frames[32] = {};
    USHORT count = CaptureStackBackTrace(0, 32, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        auto addr = reinterpret_cast<uintptr_t>(frames[i]);
        HMODULE module = nullptr;
        // GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT: this must not take
        // a new reference on the module - freeing it later would mean
        // calling FreeLibrary from inside a crash filter, which this file
        // avoids entirely.
        if (GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(addr), &module) &&
            module != nullptr) {
            wchar_t moduleName[MAX_PATH] = {};
            GetModuleFileNameW(module, moduleName, MAX_PATH);
            WriteRaw("  ");
            WriteModuleBaseName(moduleName);
            WriteRaw("+");
            WriteHex(addr - reinterpret_cast<uintptr_t>(module));
            WriteRaw("\r\n");
        } else {
            WriteRaw("  ");
            WriteHex(addr);
            WriteRaw(" (unknown module)\r\n");
        }
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
            WriteRaw("\r\n");
        }
    }
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
    if (g_crashFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(g_crashFile, 0, nullptr, FILE_BEGIN);

        SYSTEMTIME st;
        GetLocalTime(&st);
        WriteRaw("FeatherRPC crash report\r\n");
        WriteRaw("Time: ");
        WriteDec(st.wYear, 4);
        WriteRaw("-");
        WriteDec(st.wMonth, 2);
        WriteRaw("-");
        WriteDec(st.wDay, 2);
        WriteRaw(" ");
        WriteDec(st.wHour, 2);
        WriteRaw(":");
        WriteDec(st.wMinute, 2);
        WriteRaw(":");
        WriteDec(st.wSecond, 2);
        WriteRaw("\r\n");

        WriteRaw("Version: ");
        WriteRaw(core::kBuildString);
        WriteRaw("\r\n");

        WriteRaw("Thread ID: ");
        WriteDec(GetCurrentThreadId());
        WriteRaw("\r\n");

        if (info != nullptr && info->ExceptionRecord != nullptr) {
            WriteRaw("Exception code: ");
            WriteHex(info->ExceptionRecord->ExceptionCode);
            WriteRaw("\r\n");
            WriteRaw("Exception address: ");
            WriteHex(reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress));
            WriteRaw("\r\n");
        }

        WriteRaw("\r\n-- Backtrace (module + offset from module base; symbolize offline against the matching PDB) --\r\n");
        WriteBacktrace();

        WriteRaw("\r\n-- Recent log lines (best-effort; a line may be torn if the crash landed mid-write) --\r\n");
        WriteRecentLog();

        // Truncate anything left over from a shorter previous report -
        // the handle was opened once at startup without truncating (so a
        // crash from a *previous* session survives until the user's read
        // it via Copy Diagnostic Info), so this is the only point at
        // which old content is actually discarded, and only exactly up
        // to this new report's length.
        SetEndOfFile(g_crashFile);
        FlushFileBuffers(g_crashFile);
    }

    // EXCEPTION_EXECUTE_HANDLER, not EXCEPTION_CONTINUE_SEARCH - this is
    // already the top-level (last-chance) filter, so continuing search
    // would just hand off to the OS's own "stopped working" dialog with
    // no additional benefit; execute-handler lets the process exit
    // cleanly once this report is on disk.
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void InstallCrashHandler() {
    // OPEN_ALWAYS, not CREATE_ALWAYS: must not truncate a crash report
    // left behind by a previous session before this session's user has
    // had a chance to read it via Copy Diagnostic Info. FILE_SHARE_READ
    // so DiagnosticReport's normal std::ifstream read (from a completely
    // separate handle, whenever Copy Diagnostic Info runs) never hits a
    // sharing violation against this long-lived handle.
    g_crashFile = CreateFileW(core::GetCrashFilePath().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    SetUnhandledExceptionFilter(&CrashFilter);
}

}  // namespace platform_windows
