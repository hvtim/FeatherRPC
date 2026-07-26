#include "PresenceEngine.h"

#include "Log.h"

namespace core {

namespace {
constexpr const char* kPlaceholderClientId = "YOUR_DISCORD_CLIENT_ID_HERE";
}

PresenceEngine::PresenceEngine(AppConfig config, std::unique_ptr<MediaSource> mediaSource,
    std::unique_ptr<AlbumArtLookup> albumArt, TransportFactory transportFactory)
    : _config(std::move(config)),
      _mediaSource(std::move(mediaSource)),
      _albumArt(std::move(albumArt)),
      _transportFactory(std::move(transportFactory)) {}

PresenceEngine::~PresenceEngine() {
    Stop();
}

void PresenceEngine::Start() {
    if (_running.exchange(true)) {
        return;
    }
    _thread = std::thread(&PresenceEngine::RunLoop, this);
}

void PresenceEngine::Stop() {
    if (!_running.exchange(false)) {
        return;
    }
    _wakeCv.notify_all();
    if (_thread.joinable()) {
        _thread.join();
    }
    if (_discord) {
        _discord->ClearActivity();
    }
}

void PresenceEngine::UpdateConfig(AppConfig config, std::unique_ptr<MediaSource> mediaSource) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_config.clientId != config.clientId) {
            _reconnectRequested = true;
        }
        _config = std::move(config);
        if (mediaSource) {
            _pendingMediaSource = std::move(mediaSource);
        }
    }

    // Cosmetic-only changes (track number, art mode, etc.) don't change the
    // track/artist/state signature the loop checks, so without this they'd
    // silently wait for the next real track change or the 60s keepalive
    // before actually reaching Discord.
    _forceResend = true;

    {
        std::lock_guard<std::mutex> lock(_wakeMutex);
        _wakeRequested = true;
    }
    _wakeCv.notify_all();
}

std::string PresenceEngine::Status() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _status;
}

void PresenceEngine::SetStatus(std::string status) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _status = std::move(status);
    }
    if (OnStatusChanged) {
        OnStatusChanged();
    }
}

bool PresenceEngine::InterruptibleSleep(std::chrono::milliseconds duration) {
    std::unique_lock<std::mutex> lock(_wakeMutex);
    _wakeCv.wait_for(lock, duration, [this] { return !_running.load() || _wakeRequested.load(); });
    _wakeRequested = false;
    return _running.load();
}

void PresenceEngine::RunLoop() {
    while (_running.load()) {
        AppConfig config;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            config = _config;
            if (_pendingMediaSource) {
                _mediaSource = std::move(_pendingMediaSource);
            }
        }

        if (config.clientId.empty() || config.clientId == kPlaceholderClientId) {
            SetStatus("Waiting for Discord Application ID");
            if (!InterruptibleSleep(std::chrono::milliseconds(1000))) return;
            continue;
        }

        if (_reconnectRequested.exchange(false)) {
            _discord.reset();
        }

        if (!_discord) {
            _discord = std::make_unique<DiscordIpcClient>(config.clientId, _transportFactory());
        }

        if (!config.broadcastEnabled) {
            if (!_lastSignature.empty() && _discord->ClearActivity()) {
                _lastSignature.clear();
            }
            SetStatus("Disabled");
            if (!InterruptibleSleep(std::chrono::milliseconds(config.pollIntervalMs))) return;
            continue;
        }

        std::optional<TrackInfo> track = _mediaSource ? _mediaSource->GetCurrentTrack() : std::nullopt;

        if (!track.has_value() || track->state == PlaybackState::Stopped || track->name.empty()) {
            if (!_lastSignature.empty()) {
                bool cleared = _discord->ClearActivity();
                if (cleared) {
                    _lastSignature.clear();
                    SetStatus("Nothing playing");
                    Log::Write("Cleared Discord activity (nothing playing).");
                } else {
                    Log::Write("[warn] Failed to clear Discord activity - will retry.");
                }
            } else if (Status() != "Nothing playing") {
                SetStatus("Nothing playing");
            }
        } else {
            std::string pausedTag = track->state == PlaybackState::Paused ? " (Paused)" : "";
            std::string trackNumberText;
            if (config.showTrackNumber && track->trackCount > 0) {
                trackNumberText =
                    " - Track " + std::to_string(track->trackNumber) + " / " + std::to_string(track->trackCount);
            }
            std::string state = track->artist + pausedTag + trackNumberText;

            std::string signature =
                track->name + "|" + track->artist + "|" + std::to_string(static_cast<int>(track->state));
            auto now = std::chrono::system_clock::now();

            bool contentChanged = signature != _lastSignature;
            bool refreshDue = (now - _lastSentAt) >= kRefreshInterval;
            bool forceResend = _forceResend.load();

            if (contentChanged || refreshDue || forceResend) {
                _forceResend = false;

                auto start = now - std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                        std::chrono::duration<double>(track->elapsedSeconds));

                // A zero/unknown duration (some sources never report one)
                // would make end == start, rendering the activity as
                // already finished the instant it's sent - only set an
                // end time when duration is real.
                std::optional<std::chrono::system_clock::time_point> end;
                if (track->state == PlaybackState::Playing && track->durationSeconds > 0) {
                    end = start + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                      std::chrono::duration<double>(track->durationSeconds));
                }

                std::string largeImageKey;
                std::string artTag;

                if (config.artMode == "Custom" && !config.customArtUrl.empty()) {
                    largeImageKey = config.customArtUrl;
                    artTag = " [custom art]";
                } else if (config.artMode == "Off") {
                    largeImageKey = config.largeImageKey;
                } else {
                    auto artworkUrl = _albumArt ? _albumArt->GetArtworkUrl(track->artist, track->name, track->album)
                                                 : std::nullopt;
                    largeImageKey = artworkUrl.value_or(config.largeImageKey);
                    artTag = artworkUrl.has_value() ? " [cover art]" : "";
                }

                bool sent = _discord->SetActivity(track->artist, track->name, state, start, end, largeImageKey,
                    track->album.empty() ? "Now Playing" : track->album);

                std::string kind = (refreshDue && !contentChanged) ? " (keepalive)" : "";

                if (sent) {
                    _lastSignature = signature;
                    _lastSentAt = now;
                    SetStatus(track->name + " - " + track->artist);
                    Log::Write("Updated: " + track->name + " - " + state + artTag + kind + " [" + largeImageKey + "]");
                } else {
                    Log::Write("[warn] Failed to send activity update for " + track->name + " - will retry next poll.");
                }
            }
        }

        if (!InterruptibleSleep(std::chrono::milliseconds(config.pollIntervalMs))) return;
    }
}

} // namespace core
