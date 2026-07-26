#include "DiscordIpcClient.h"

#include <nlohmann/json.hpp>

#include <atomic>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace core {

namespace {

constexpr int32_t kOpHandshake = 0;
constexpr int32_t kOpFrame = 1;
// Never trust the declared frame length enough to allocate it blindly - a
// malformed frame could otherwise trigger a huge allocation. Carried over
// from the C# DiscordIpcClient.ReadFrame security fix.
constexpr uint32_t kMaxFrameSize = 1024 * 1024;

int64_t CurrentProcessId() {
#ifdef _WIN32
    return static_cast<int64_t>(GetCurrentProcessId());
#else
    return static_cast<int64_t>(getpid());
#endif
}

int64_t ToUnixSeconds(std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
}

std::string MakeNonce() {
    // Only used by Discord to correlate responses, which this client
    // doesn't read back - a monotonically increasing counter is enough.
    static std::atomic<uint64_t> counter{0};
    return std::to_string(++counter);
}

} // namespace

DiscordIpcClient::DiscordIpcClient(std::string clientId, std::unique_ptr<IpcTransport> transport)
    : _clientId(std::move(clientId)), _transport(std::move(transport)), _pid(CurrentProcessId()) {}

bool DiscordIpcClient::IsConnected() const {
    return _transport->IsConnected();
}

bool DiscordIpcClient::Connect() {
    if (IsConnected()) {
        return true;
    }

    for (int i = 0; i < 10; ++i) {
        if (!_transport->Connect(i)) {
            continue;
        }

        nlohmann::json handshake = {{"v", 1}, {"client_id", _clientId}};
        if (WriteFrame(kOpHandshake, handshake.dump()) && ReadFrame().has_value()) {
            return true;
        }
        _transport->Disconnect();
    }

    return false;
}

void DiscordIpcClient::Disconnect() {
    _transport->Disconnect();
}

bool DiscordIpcClient::SetActivity(const std::string& name, const std::string& details, const std::string& state,
    std::chrono::system_clock::time_point start, std::optional<std::chrono::system_clock::time_point> end,
    const std::string& largeImageKey, const std::string& largeImageText) {
    if (!IsConnected() && !Connect()) {
        return false;
    }

    nlohmann::json timestamps = {{"start", ToUnixSeconds(start)}};
    if (end.has_value()) {
        timestamps["end"] = ToUnixSeconds(*end);
    }

    nlohmann::json payload = {
        {"cmd", "SET_ACTIVITY"},
        {"args", {
            {"pid", _pid},
            {"activity", {
                {"name", name},
                {"type", 2}, // Listening - renders as "Listening to <name>"
                {"details", details},
                {"state", state},
                {"timestamps", timestamps},
                {"assets", {{"large_image", largeImageKey}, {"large_text", largeImageText}}},
            }},
        }},
        {"nonce", MakeNonce()},
    };

    return TrySend(payload.dump());
}

bool DiscordIpcClient::ClearActivity() {
    if (!IsConnected()) {
        return false;
    }

    nlohmann::json payload = {
        {"cmd", "SET_ACTIVITY"},
        {"args", {{"pid", _pid}}},
        {"nonce", MakeNonce()},
    };

    return TrySend(payload.dump());
}

bool DiscordIpcClient::TrySend(const std::string& json) {
    if (!WriteFrame(kOpFrame, json) || !ReadFrame().has_value()) {
        // Discord likely closed/restarted - drop the connection so the next call reconnects.
        _transport->Disconnect();
        return false;
    }
    return true;
}

bool DiscordIpcClient::WriteFrame(int32_t opcode, const std::string& json) {
    uint32_t header[2] = {static_cast<uint32_t>(opcode), static_cast<uint32_t>(json.size())};
    if (!_transport->WriteExact(header, sizeof(header))) {
        return false;
    }
    if (!json.empty() && !_transport->WriteExact(json.data(), json.size())) {
        return false;
    }
    return true;
}

std::optional<std::string> DiscordIpcClient::ReadFrame() {
    uint32_t header[2];
    if (!_transport->ReadExact(header, sizeof(header))) {
        return std::nullopt;
    }

    uint32_t length = header[1];
    if (length > kMaxFrameSize) {
        return std::nullopt;
    }

    std::string payload(length, '\0');
    if (length > 0 && !_transport->ReadExact(payload.data(), length)) {
        return std::nullopt;
    }
    return payload;
}

} // namespace core
