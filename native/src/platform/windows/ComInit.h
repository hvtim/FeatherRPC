#pragma once

namespace platform_windows {

// Ensures COM (STA) is initialized on the calling thread - shared by both
// the iTunes COM automation and the SMTC C++/WinRT calls, which must use
// the same apartment type on a given thread or the second one to
// initialize fails with RPC_E_CHANGED_MODE. Safe to call repeatedly; only
// the first call per thread does anything. Never paired with a matching
// uninitialize call since the only caller is PresenceEngine's single
// long-lived worker thread, torn down only at process exit anyway.
void EnsureComInitialized();

} // namespace platform_windows
