#pragma once

namespace platform_posix {

// Installs sigaction handlers for SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS
// writing a crash report to core::GetCrashFilePath(). Shared between
// Linux and macOS. Call once, early in main()/main.mm, right after
// core::Log::Init() - see CrashHandler.cpp for the signal-safety design.
void InstallCrashHandler();

}  // namespace platform_posix
