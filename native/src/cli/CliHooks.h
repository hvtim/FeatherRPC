#pragma once

#include "core/AutoLaunch.h"
#include "core/DaemonSignal.h"
#include "core/MediaSource.h"

#include <functional>
#include <memory>
#include <vector>

namespace cli {

// Platform-injected dependencies the portable command dispatcher needs -
// mirrors PresenceEngine's own constructor-injection style so cli/ never
// depends on platform/, matching the rule core/ already follows.
struct Hooks {
    std::unique_ptr<core::AutoLaunch> autoLaunch;
    std::unique_ptr<core::DaemonSignal> daemonSignal;

    // Stateless - reads live sources directly, no running-instance
    // round-trip needed. Empty/unset on platforms with nothing to
    // enumerate (macOS: Music.app is the only source).
    std::function<std::vector<core::MediaSourceInfo>()> listMediaSources;

    // Spawns a detached headless instance of the app binary. Used by
    // `featherrpc daemon start`. Returns false if spawning failed.
    std::function<bool()> spawnDaemon;
};

} // namespace cli
