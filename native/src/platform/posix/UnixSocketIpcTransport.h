#pragma once

#include "core/IpcTransport.h"

namespace platform_posix {

// Unix domain socket transport for Discord's local IPC protocol - the
// macOS/Linux counterpart to platform/windows/PipeIpcTransport. Shared
// between both since the logic is identical on any POSIX platform.
class UnixSocketIpcTransport : public core::IpcTransport {
public:
    ~UnixSocketIpcTransport() override;

    bool Connect(int index) override;
    void Disconnect() override;
    bool IsConnected() const override;

    bool WriteExact(const void* data, size_t size) override;
    bool ReadExact(void* data, size_t size) override;

private:
    int _fd = -1;
};

} // namespace platform_posix
