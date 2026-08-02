#include "MprisMediaSource.h"
#include "core/Log.h"

#include <dbus/dbus.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace platform_linux {

namespace {

constexpr const char* kBusPrefix = "org.mpris.MediaPlayer2.";
constexpr const char* kPlayerPath = "/org/mpris/MediaPlayer2";
constexpr const char* kRootIface = "org.mpris.MediaPlayer2";
constexpr const char* kPlayerIface = "org.mpris.MediaPlayer2.Player";
constexpr int kCallTimeoutMs = 500;

bool IsSpotify(const std::string& appId) {
    std::string lower = appId;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("spotify") != std::string::npos;
}

// dbus_bus_get() returns a process-wide shared connection owned by
// libdbus, not a new one per call - must not be unreffed here (that's
// only paired with an explicit dbus_connection_ref()).
DBusConnection* GetSessionBus() {
    static DBusConnection* connection = [] {
        DBusError error;
        dbus_error_init(&error);
        DBusConnection* conn = dbus_bus_get(DBUS_BUS_SESSION, &error);
        if (dbus_error_is_set(&error)) {
            // Only ever runs once per process (this whole lambda is a
            // static initializer), so unconditional logging here can't
            // spam - and if the session bus is genuinely unreachable,
            // every media-source call for the rest of the process's life
            // silently returns nothing without this, with no way to tell
            // that apart from "no player is running".
            core::Log::Write(std::string("[warn] MPRIS: couldn't connect to the D-Bus session bus (") +
                              error.message + ") - media source detection will not work this session.");
            dbus_error_free(&error);
        }
        return conn;
    }();
    return connection;
}

// Calls org.freedesktop.DBus.Properties.Get and returns the raw reply
// (still wrapped in its outer variant) - callers recurse into it with the
// Read* helpers below based on the expected type. errorOut, if given, is
// set to the real DBusError message on failure - callers use this to tell
// a genuine D-Bus problem apart from "the player just isn't running right
// now" (e.g. org.freedesktop.DBus.Error.ServiceUnknown), which is the
// overwhelmingly common, totally expected reason this fails.
DBusMessage* GetProperty(DBusConnection* conn, const std::string& busName, const std::string& path,
    const std::string& iface, const std::string& prop, std::string* errorOut = nullptr) {
    DBusMessage* msg = dbus_message_new_method_call(
        busName.c_str(), path.c_str(), "org.freedesktop.DBus.Properties", "Get");
    if (!msg) return nullptr;

    const char* ifaceC = iface.c_str();
    const char* propC = prop.c_str();
    DBusMessageIter args;
    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &ifaceC);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &propC);

    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, kCallTimeoutMs, &error);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&error)) {
        if (errorOut) {
            *errorOut = error.name ? (std::string(error.name) + ": " + (error.message ? error.message : "")) : "";
        }
        dbus_error_free(&error);
    }
    return reply;
}

std::string ReadVariantString(DBusMessage* reply) {
    DBusMessageIter iter, variant;
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT) return "";
    dbus_message_iter_recurse(&iter, &variant);
    if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_STRING) return "";
    const char* s = nullptr;
    dbus_message_iter_get_basic(&variant, &s);
    return s ? s : "";
}

// Position/mpris:length are spec'd as int64, but some players report
// uint64 - accepted defensively since this mismatch is a known real-world
// MPRIS interop wrinkle, not a hypothetical.
dbus_int64_t ReadVariantInt64(DBusMessage* reply) {
    DBusMessageIter iter, variant;
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT) return 0;
    dbus_message_iter_recurse(&iter, &variant);
    int type = dbus_message_iter_get_arg_type(&variant);
    if (type == DBUS_TYPE_INT64) {
        dbus_int64_t v = 0;
        dbus_message_iter_get_basic(&variant, &v);
        return v;
    }
    if (type == DBUS_TYPE_UINT64) {
        dbus_uint64_t v = 0;
        dbus_message_iter_get_basic(&variant, &v);
        return static_cast<dbus_int64_t>(v);
    }
    return 0;
}

void ReadMetadataInto(DBusMessage* reply, core::TrackInfo& info) {
    DBusMessageIter iter, variant, dict;
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT) return;
    dbus_message_iter_recurse(&iter, &variant);
    if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_ARRAY) return;
    dbus_message_iter_recurse(&variant, &dict);

    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&dict, &entry);

        const char* key = nullptr;
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);

        DBusMessageIter value;
        dbus_message_iter_recurse(&entry, &value);
        int valueType = dbus_message_iter_get_arg_type(&value);
        std::string k = key ? key : "";

        if (k == "xesam:title" && valueType == DBUS_TYPE_STRING) {
            const char* s = nullptr;
            dbus_message_iter_get_basic(&value, &s);
            info.name = s ? s : "";
        } else if (k == "xesam:album" && valueType == DBUS_TYPE_STRING) {
            const char* s = nullptr;
            dbus_message_iter_get_basic(&value, &s);
            info.album = s ? s : "";
        } else if (k == "xesam:artist" && valueType == DBUS_TYPE_ARRAY) {
            DBusMessageIter artistArr;
            dbus_message_iter_recurse(&value, &artistArr);
            std::string joined;
            while (dbus_message_iter_get_arg_type(&artistArr) == DBUS_TYPE_STRING) {
                const char* s = nullptr;
                dbus_message_iter_get_basic(&artistArr, &s);
                if (!joined.empty()) joined += ", ";
                joined += s ? s : "";
                if (!dbus_message_iter_next(&artistArr)) break;
            }
            info.artist = joined;
        } else if (k == "mpris:length" && (valueType == DBUS_TYPE_INT64 || valueType == DBUS_TYPE_UINT64)) {
            dbus_int64_t microseconds = 0;
            if (valueType == DBUS_TYPE_INT64) {
                dbus_message_iter_get_basic(&value, &microseconds);
            } else {
                dbus_uint64_t u = 0;
                dbus_message_iter_get_basic(&value, &u);
                microseconds = static_cast<dbus_int64_t>(u);
            }
            info.durationSeconds = static_cast<double>(microseconds) / 1'000'000.0;
        } else if (k == "xesam:trackNumber" && valueType == DBUS_TYPE_INT32) {
            dbus_int32_t n = 0;
            dbus_message_iter_get_basic(&value, &n);
            info.trackNumber = n;
        }

        if (!dbus_message_iter_next(&dict)) break;
    }
}

// Gates GetAvailableSources()'s ListNames-failure logging to state
// transitions only - see MprisMediaSource::_lastGetCurrentTrackFailed for
// why. File-scope static rather than a member since GetAvailableSources()
// is itself static, called before any instance necessarily exists (to
// populate the tray's Media Source submenu).
bool s_lastListNamesFailed = false;

} // namespace

MprisMediaSource::MprisMediaSource(std::string busName) : _busName(std::move(busName)) {}

std::vector<core::MediaSourceInfo> MprisMediaSource::GetAvailableSources() {
    std::vector<core::MediaSourceInfo> result;

    DBusConnection* conn = GetSessionBus();
    if (!conn) return result;

    DBusMessage* msg =
        dbus_message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames");
    if (!msg) return result;

    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, kCallTimeoutMs, &error);
    dbus_message_unref(msg);
    if (!reply) {
        // Runs once per menu-open, not once per poll, but should still
        // stay quiet if the session bus is genuinely, persistently down -
        // logged only on the failed->failed edge.
        if (!s_lastListNamesFailed) {
            core::Log::Write(std::string("[warn] MPRIS: listing D-Bus names failed (") +
                              (error.message ? error.message : "unknown error") +
                              ") - the Media Source menu will show no players until this recovers.");
            s_lastListNamesFailed = true;
        }
        dbus_error_free(&error);
        return result;
    }
    if (s_lastListNamesFailed) {
        core::Log::Write("MPRIS: listing D-Bus names recovered - succeeded again.");
        s_lastListNamesFailed = false;
    }

    DBusMessageIter iter, arr;
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&iter, &arr);
        while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING) {
            const char* name = nullptr;
            dbus_message_iter_get_basic(&arr, &name);
            std::string busName = name ? name : "";

            if (busName.rfind(kBusPrefix, 0) == 0) {
                std::string appId = busName.substr(std::strlen(kBusPrefix));
                if (!IsSpotify(appId)) {
                    std::string identity;
                    if (DBusMessage* identityReply = GetProperty(conn, busName, kPlayerPath, kRootIface, "Identity")) {
                        identity = ReadVariantString(identityReply);
                        dbus_message_unref(identityReply);
                    }
                    result.push_back({busName, identity.empty() ? appId : identity});
                }
            }

            if (!dbus_message_iter_next(&arr)) break;
        }
    }

    dbus_message_unref(reply);
    return result;
}

std::optional<core::TrackInfo> MprisMediaSource::GetCurrentTrack() {
    DBusConnection* conn = GetSessionBus();
    if (!conn) return std::nullopt;

    // A failed property fetch here almost always means the player quit
    // since it was last enumerated - same "just report nothing" handling
    // as the Windows SMTC source's session-not-found case. Still worth
    // logging the real reason on the failed->failed edge (not every poll)
    // though: this is exactly the log line that tells a bug reporter
    // "player closed" (ServiceUnknown/NameHasNoOwner) apart from a
    // genuine D-Bus problem, instead of both looking identically like
    // silent nothing.
    std::string statusError;
    DBusMessage* statusReply = GetProperty(conn, _busName, kPlayerPath, kPlayerIface, "PlaybackStatus", &statusError);
    if (!statusReply) {
        if (!_lastGetCurrentTrackFailed) {
            core::Log::Write("[warn] MPRIS: reading PlaybackStatus for " + _busName + " failed (" +
                              (statusError.empty() ? "no error detail available" : statusError) +
                              ") - will keep retrying silently.");
            _lastGetCurrentTrackFailed = true;
        }
        return std::nullopt;
    }
    if (_lastGetCurrentTrackFailed) {
        core::Log::Write("MPRIS: reading " + _busName + " recovered - succeeded again.");
        _lastGetCurrentTrackFailed = false;
    }
    std::string statusStr = ReadVariantString(statusReply);
    dbus_message_unref(statusReply);

    core::PlaybackState state;
    if (statusStr == "Playing") {
        state = core::PlaybackState::Playing;
    } else if (statusStr == "Paused") {
        state = core::PlaybackState::Paused;
    } else {
        state = core::PlaybackState::Stopped;
    }

    if (state == core::PlaybackState::Stopped) {
        return core::TrackInfo{};
    }

    core::TrackInfo info;
    info.state = state;

    if (DBusMessage* metaReply = GetProperty(conn, _busName, kPlayerPath, kPlayerIface, "Metadata")) {
        ReadMetadataInto(metaReply, info);
        dbus_message_unref(metaReply);
    }

    // Position is queried live via property-get, unlike SMTC's discrete
    // push updates - it already reflects "now", so no elapsed-time
    // extrapolation is needed here.
    if (DBusMessage* posReply = GetProperty(conn, _busName, kPlayerPath, kPlayerIface, "Position")) {
        info.elapsedSeconds = static_cast<double>(ReadVariantInt64(posReply)) / 1'000'000.0;
        dbus_message_unref(posReply);
    }

    return info;
}

} // namespace platform_linux
