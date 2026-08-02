#include "Clipboard.h"
#include "core/ConfigPaths.h"
#include "core/Log.h"

#include <gio/gio.h>

#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string_view>
#include <vector>

namespace platform_linux {

namespace {

// access() instead of forking a shell to run `command -v` - avoids up to
// 3 shell spawns per click (wl-copy/xclip/xsel are each tried in turn).
bool CommandExists(const char* cmd) {
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) {
        return false;
    }
    std::string_view path(pathEnv);
    size_t start = 0;
    while (start <= path.size()) {
        size_t colon = path.find(':', start);
        std::string_view dir = path.substr(start, colon == std::string_view::npos ? std::string_view::npos : colon - start);
        if (!dir.empty()) {
            std::string candidate(dir);
            candidate += '/';
            candidate += cmd;
            if (access(candidate.c_str(), X_OK) == 0) {
                return true;
            }
        }
        if (colon == std::string_view::npos) {
            break;
        }
        start = colon + 1;
    }
    return false;
}

// $DISPLAY alone isn't reliable - XWayland sets it too. $WAYLAND_DISPLAY
// is the standard signal for which clipboard mechanism is actually live.
bool IsWayland() {
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    return wayland != nullptr && *wayland != '\0';
}

// Writing to a subprocess stdin whose reader is gone raises SIGPIPE,
// which kills the process by default - one bad clipboard tool shouldn't
// take down the tray.
void EnsureSigpipeIgnored() {
    static bool once = [] {
        signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)once;
}

// One attempt through the wl-copy -> xclip -> xsel -> file fallback
// chain. Threaded through as GSubprocess's user_data, so its lifetime is
// tied to the pending async operation.
struct ClipboardAttempt {
    std::string text;
    std::vector<std::string> remainingCommands;
};

// g_subprocess_communicate_async() only completes once the child exits -
// fine for xclip/xsel, but wl-copy is designed to never exit, so the
// callback would never fire and this would leak indefinitely. Bound the
// wait with a GCancellable: cancelling it doesn't touch the child process
// (confirmed against gsubprocess.c), it only abandons our own wait.
constexpr guint kToolPatienceMs = 200;

struct PendingCommand {
    std::unique_ptr<ClipboardAttempt> attempt;
    GCancellable* cancellable;
    guint timeoutId;
};

void TryNextClipboardCommand(std::unique_ptr<ClipboardAttempt> attempt);

void WriteFallbackFile(const std::string& text) {
    std::filesystem::path path = core::GetConfigDirectory() / "featherrpc-diagnostic.txt";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        core::Log::Write(
            "[warn] Copy Diagnostic Info: no clipboard tool succeeded, and couldn't write the fallback file either.");
        return;
    }
    file << text;
    core::Log::Write(
        "[warn] Copy Diagnostic Info: no clipboard tool succeeded - wrote the report to " + path.string() +
        " instead.");
}

gboolean OnCommandTimeout(gpointer userData) {
    auto* pending = static_cast<PendingCommand*>(userData);
    // Zeroed before cancelling: g_cancellable_cancel() may re-enter
    // OnClipboardCommandDone synchronously, which must see timeoutId==0.
    pending->timeoutId = 0;
    g_cancellable_cancel(pending->cancellable);
    return G_SOURCE_REMOVE;
}

void OnClipboardCommandDone(GObject* sourceObject, GAsyncResult* result, gpointer userData) {
    auto* pending = static_cast<PendingCommand*>(userData);
    GSubprocess* subprocess = G_SUBPROCESS(sourceObject);

    GError* error = nullptr;
    bool exitedForReal = g_subprocess_communicate_finish(subprocess, result, nullptr, nullptr, &error);

    if (pending->timeoutId != 0) {
        g_source_remove(pending->timeoutId);
    }
    g_object_unref(pending->cancellable);

    bool treatAsSuccess;
    if (exitedForReal) {
        treatAsSuccess = g_subprocess_get_successful(subprocess);
    } else {
        // Our timeout cancelled the wait - the tool is still running
        // (expected for wl-copy) and untouched by the cancellation, so
        // treat it as success.
        treatAsSuccess = true;
    }
    if (error) {
        g_error_free(error);
    }
    g_object_unref(subprocess);

    std::unique_ptr<ClipboardAttempt> attempt = std::move(pending->attempt);
    delete pending;

    if (treatAsSuccess) {
        return;
    }
    TryNextClipboardCommand(std::move(attempt));
}

void TryNextClipboardCommand(std::unique_ptr<ClipboardAttempt> attempt) {
    if (attempt->remainingCommands.empty()) {
        WriteFallbackFile(attempt->text);
        return;
    }

    std::string command = std::move(attempt->remainingCommands.front());
    attempt->remainingCommands.erase(attempt->remainingCommands.begin());

    GError* error = nullptr;
    GSubprocess* subprocess = g_subprocess_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                       G_SUBPROCESS_FLAGS_STDERR_SILENCE),
        &error, "/bin/sh", "-c", command.c_str(), nullptr);
    if (!subprocess) {
        if (error) {
            g_error_free(error);
        }
        TryNextClipboardCommand(std::move(attempt));
        return;
    }

    auto* pending = new PendingCommand{std::move(attempt), g_cancellable_new(), 0};
    pending->timeoutId = g_timeout_add(kToolPatienceMs, OnCommandTimeout, pending);

    // g_subprocess_communicate_async() takes its own reference on the
    // input GBytes, so unref'ing right after is correct.
    GBytes* stdinBytes = g_bytes_new(pending->attempt->text.data(), pending->attempt->text.size());
    g_subprocess_communicate_async(subprocess, stdinBytes, pending->cancellable, OnClipboardCommandDone, pending);
    g_bytes_unref(stdinBytes);
}

}  // namespace

void CopyToClipboard(const std::string& text) {
    EnsureSigpipeIgnored();

    auto attempt = std::make_unique<ClipboardAttempt>();
    attempt->text = text;
    if (IsWayland() && CommandExists("wl-copy")) {
        attempt->remainingCommands.push_back("wl-copy");
    }
    if (CommandExists("xclip")) {
        attempt->remainingCommands.push_back("xclip -selection clipboard");
    }
    if (CommandExists("xsel")) {
        attempt->remainingCommands.push_back("xsel --clipboard --input");
    }
    TryNextClipboardCommand(std::move(attempt));
}

}  // namespace platform_linux
