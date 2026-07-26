#pragma once

#include "AlbumArtLookup.h"
#include "AppConfig.h"
#include "DiscordIpcClient.h"
#include "MediaSource.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace core {

// Runs the media-source -> Discord polling loop on its own worker thread,
// independent of whatever UI (tray icon) is driving it. Config/media
// source can be swapped out live via UpdateConfig - only the worker
// thread ever touches _mediaSource/_discord directly (it picks up
// pending swaps at the top of each loop iteration), so a menu action on
// another thread never races a poll in progress.
class PresenceEngine {
public:
    using TransportFactory = std::function<std::unique_ptr<IpcTransport>()>;

    PresenceEngine(AppConfig config, std::unique_ptr<MediaSource> mediaSource,
        std::unique_ptr<AlbumArtLookup> albumArt, TransportFactory transportFactory);
    ~PresenceEngine();

    void Start();
    void Stop();

    // mediaSource is optional - pass nullptr if only config fields
    // changed and the media source selection itself didn't.
    void UpdateConfig(AppConfig config, std::unique_ptr<MediaSource> mediaSource);

    std::string Status() const;

    // Invoked from the worker thread whenever Status() changes - the tray
    // layer marshals this onto its own message loop thread (see the
    // plan's Threading section).
    std::function<void()> OnStatusChanged;

private:
    static constexpr std::chrono::seconds kRefreshInterval{60};

    void RunLoop();
    void SetStatus(std::string status);
    // Returns false if Stop() was called during the wait (caller should exit its loop).
    bool InterruptibleSleep(std::chrono::milliseconds duration);

    AppConfig _config;
    std::unique_ptr<MediaSource> _mediaSource; // worker-thread-only after Start()
    std::unique_ptr<MediaSource> _pendingMediaSource; // guarded by _mutex
    std::unique_ptr<AlbumArtLookup> _albumArt;
    std::unique_ptr<DiscordIpcClient> _discord; // worker-thread-only
    TransportFactory _transportFactory;
    std::string _status = "Starting...";

    mutable std::mutex _mutex; // guards _config, _pendingMediaSource, _status
    std::atomic<bool> _reconnectRequested{false};
    std::atomic<bool> _forceResend{false};

    std::string _lastSignature; // worker-thread-only
    // Epoch (1970), not time_point::min() - "now - _lastSentAt" below would
    // overflow system_clock::duration's range if min() were used here.
    std::chrono::system_clock::time_point _lastSentAt{};

    std::thread _thread;
    std::atomic<bool> _running{false};
    std::atomic<bool> _wakeRequested{false};
    std::condition_variable _wakeCv;
    std::mutex _wakeMutex;
};

} // namespace core
