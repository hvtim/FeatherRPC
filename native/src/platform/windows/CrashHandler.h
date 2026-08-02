#pragma once

namespace platform_windows {

// Installs a process-wide SetUnhandledExceptionFilter writing a crash
// report to core::GetCrashFilePath(). Call once, early in wWinMain, right
// after core::Log::Init() - the file handle is opened here so the filter
// itself never has to do file-open work on a possibly-corrupted process.
void InstallCrashHandler();

}  // namespace platform_windows
