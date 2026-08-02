#include "CrashHandler.h"

#include "core/ConfigPaths.h"
#include "core/CrashRing.h"
#include "core/Version.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

// No DbgHelp/SymFromAddr here - a Release build ships no PDB, and symbol
// lookup risks faulting again on an already-corrupted process. Frames
// resolve to module+offset only, symbolized later offline (see
// docs/Releasing.md). No heap allocation anywhere (no snprintf/
// std::string) - the CRT heap lock may be held by the thread that just
// faulted, so new/malloc here risks deadlock instead of ever reporting.

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

// The first several frames are always this function's own call plus the
// OS's exception-dispatch machinery, not a bug - skip past the
// CrashHandler.cpp/ntdll/KERNELBASE frames at the top to find the real fault site.
void WriteBacktrace() {
    void* frames[32] = {};
    USHORT count = CaptureStackBackTrace(0, 32, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        auto addr = reinterpret_cast<uintptr_t>(frames[i]);
        HMODULE module = nullptr;
        // UNCHANGED_REFCOUNT: no new module reference, so nothing here ever
        // needs to call FreeLibrary from inside a crash filter.
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

        // Truncate to this report's length - the file wasn't opened with
        // truncation so a previous session's crash survives until read.
        SetEndOfFile(g_crashFile);
        FlushFileBuffers(g_crashFile);
    }

    // This is the last-chance filter, so EXECUTE_HANDLER lets the process
    // exit cleanly once the report is on disk, instead of CONTINUE_SEARCH
    // just handing off to the OS's "stopped working" dialog.
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void InstallCrashHandler() {
    // OPEN_ALWAYS: don't truncate a previous session's unread crash report.
    // FILE_SHARE_READ: let DiagnosticReport read it concurrently.
    g_crashFile = CreateFileW(core::GetCrashFilePath().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    SetUnhandledExceptionFilter(&CrashFilter);
}

}  // namespace platform_windows
