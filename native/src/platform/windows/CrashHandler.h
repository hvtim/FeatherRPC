#pragma once

namespace platform_windows {

// Installs a process-wide SetUnhandledExceptionFilter that writes a
// best-effort crash report (build version, OS, faulting thread/exception,
// a raw module+offset backtrace, and the last few log lines) to
// core::GetCrashFilePath() - deliberately a separate file from the main
// log (see that function's comment for why).
//
// Call once, early in wWinMain, right after core::Log::Init() - the crash
// file handle is opened here, up front, specifically so the actual filter
// never has to do file-open work while the process may be in a corrupted
// state (e.g. the CRT heap lock held by the faulting thread).
void InstallCrashHandler();

}  // namespace platform_windows
