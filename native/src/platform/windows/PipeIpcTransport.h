#pragma once

#include "core/IpcTransport.h"

#include <windows.h>

namespace platform_windows {

class PipeIpcTransport : public core::IpcTransport {
public:
    ~PipeIpcTransport() override;

    bool Connect(int index) override;
    void Disconnect() override;
    bool IsConnected() const override;

    bool WriteExact(const void* data, size_t size) override;
    bool ReadExact(void* data, size_t size) override;

private:
    HANDLE _pipe = INVALID_HANDLE_VALUE;
};

} // namespace platform_windows
