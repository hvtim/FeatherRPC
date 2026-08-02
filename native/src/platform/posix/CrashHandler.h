#pragma once

namespace platform_posix {

// Installs sigaction handlers for SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS
// that write a best-effort crash report (build version, OS via uname(),
// the faulting signal/address, a raw backtrace, and the last few log
// lines) to core::GetCrashFilePath() - shared between Linux and macOS,
// since POSIX signal handling doesn't differ between them for anything
// this needs.
//
// Call once, early in main()/main.mm, right after core::Log::Init() - the
// crash file descriptor and the sigaltstack buffer are both set up here,
// up front, specifically so the handler itself never has to do anything
// beyond strictly async-signal-safe operations. See CrashHandler.cpp for
// the full design rationale (and DaemonSignal.cpp's own comment on why
// *that* file uses sigwait() on a dedicated thread instead of a handler
// for graceful signals - the two mechanisms have fundamentally different
// constraints, which is why they're not unified).
void InstallCrashHandler();

}  // namespace platform_posix
