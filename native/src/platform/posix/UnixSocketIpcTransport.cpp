#include "UnixSocketIpcTransport.h"
#include "core/Log.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

namespace platform_posix {

namespace {

// Resolution order matches the plan and Discord's own client: first set,
// non-empty env var wins, falling back to /tmp.
std::string ResolveSocketDir() {
    for (const char* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        if (const char* value = std::getenv(var); value && *value) {
            return value;
        }
    }
    return "/tmp";
}

} // namespace

UnixSocketIpcTransport::~UnixSocketIpcTransport() {
    Disconnect();
}

bool UnixSocketIpcTransport::Connect(int index) {
    Disconnect();

    std::string dir = ResolveSocketDir();
    if (!dir.empty() && dir.back() != '/') {
        dir += '/';
    }
    std::string path = dir + "discord-ipc-" + std::to_string(index);

    _fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (_fd < 0) {
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        Disconnect();
        return false;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

    if (connect(_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        // Flatpak/Snap Discord installs (Linux-only) sandbox their runtime
        // dir and nest the socket elsewhere (e.g. under
        // app/com.discordapp.Discord/) - not handled in v1, so a
        // first-index ENOENT is ambiguous between "Discord isn't running"
        // and "Discord is sandboxed" without probing those paths too.
        // Logged once here rather than failing silently.
        if (index == 0 && errno == ENOENT) {
            core::Log::Write(
                "[info] No Discord IPC socket found at " + path +
                " - if Discord is installed via Flatpak or Snap, its socket may be sandboxed elsewhere and isn't detected yet.");
        }
        Disconnect();
        return false;
    }

    return true;
}

void UnixSocketIpcTransport::Disconnect() {
    if (_fd >= 0) {
        close(_fd);
        _fd = -1;
    }
}

bool UnixSocketIpcTransport::IsConnected() const {
    return _fd >= 0;
}

bool UnixSocketIpcTransport::WriteExact(const void* data, size_t size) {
    const auto* p = static_cast<const char*>(data);
    size_t written = 0;
    while (written < size) {
        ssize_t chunk = write(_fd, p + written, size - written);
        if (chunk < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (chunk == 0) return false;
        written += static_cast<size_t>(chunk);
    }
    return true;
}

bool UnixSocketIpcTransport::ReadExact(void* data, size_t size) {
    auto* p = static_cast<char*>(data);
    size_t readTotal = 0;
    while (readTotal < size) {
        ssize_t chunk = read(_fd, p + readTotal, size - readTotal);
        if (chunk < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (chunk == 0) return false;
        readTotal += static_cast<size_t>(chunk);
    }
    return true;
}

} // namespace platform_posix
