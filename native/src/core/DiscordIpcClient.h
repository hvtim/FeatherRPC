#pragma once

#include "IpcTransport.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace core {

// Client for Discord's local Rich Presence IPC protocol (documented by
// the archived discord-rpc project). Hand-rolled instead of using a
// wrapper library because most wrapper libraries hardcode activity.type
// to 0 (Playing) - this needs type 2 (Listening) for "Listening to
// <name>", which is undocumented but confirmed to work.
class DiscordIpcClient {
public:
    DiscordIpcClient(std::string clientId, std::unique_ptr<IpcTransport> transport);

    bool IsConnected() const;
    bool Connect();
    void Disconnect();

    bool SetActivity(const std::string& name, const std::string& details, const std::string& state,
        std::chrono::system_clock::time_point start,
        std::optional<std::chrono::system_clock::time_point> end,
        const std::string& largeImageKey, const std::string& largeImageText);

    bool ClearActivity();

private:
    bool TrySend(const std::string& json);
    bool WriteFrame(int32_t opcode, const std::string& json);
    std::optional<std::string> ReadFrame();

    std::string _clientId;
    std::unique_ptr<IpcTransport> _transport;
    int64_t _pid;
};

} // namespace core
