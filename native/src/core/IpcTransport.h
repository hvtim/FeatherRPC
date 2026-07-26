#pragma once

#include <cstddef>

namespace core {

// Abstracts the local transport Discord's IPC protocol runs over - a
// named pipe on Windows, a Unix domain socket on macOS/Linux (same
// protocol either way: 8-byte header + JSON payload).
class IpcTransport {
public:
    virtual ~IpcTransport() = default;

    // Tries to connect to the Nth Discord IPC endpoint (index 0-9, e.g.
    // "discord-ipc-{index}"). DiscordIpcClient owns the retry loop across
    // indices; this just attempts one.
    virtual bool Connect(int index) = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;

    virtual bool WriteExact(const void* data, size_t size) = 0;
    virtual bool ReadExact(void* data, size_t size) = 0;
};

} // namespace core
