#include "PipeIpcTransport.h"

#include <string>

namespace platform_windows {

PipeIpcTransport::~PipeIpcTransport() {
    Disconnect();
}

bool PipeIpcTransport::Connect(int index) {
    Disconnect();

    std::wstring pipeName = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(index);
    _pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, 0, nullptr);
    return _pipe != INVALID_HANDLE_VALUE;
}

void PipeIpcTransport::Disconnect() {
    if (_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(_pipe);
        _pipe = INVALID_HANDLE_VALUE;
    }
}

bool PipeIpcTransport::IsConnected() const {
    return _pipe != INVALID_HANDLE_VALUE;
}

bool PipeIpcTransport::WriteExact(const void* data, size_t size) {
    const auto* p = static_cast<const char*>(data);
    DWORD written = 0;
    while (written < size) {
        DWORD chunk = 0;
        if (!WriteFile(_pipe, p + written, static_cast<DWORD>(size - written), &chunk, nullptr) || chunk == 0) {
            return false;
        }
        written += chunk;
    }
    return true;
}

bool PipeIpcTransport::ReadExact(void* data, size_t size) {
    auto* p = static_cast<char*>(data);
    DWORD readTotal = 0;
    while (readTotal < size) {
        DWORD chunk = 0;
        if (!ReadFile(_pipe, p + readTotal, static_cast<DWORD>(size - readTotal), &chunk, nullptr) || chunk == 0) {
            return false;
        }
        readTotal += chunk;
    }
    return true;
}

} // namespace platform_windows
