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

// Searches $PATH directly via access() rather than forking a shell to run
// `command -v` - CopyToClipboard tries up to 3 of these in a row
// (wl-copy/xclip/xsel), so the old approach meant up to 3 shell spawns on
// every single "Copy Diagnostic Info" click.
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

// Wayland compositors generally still run an XWayland server, so $DISPLAY
// alone isn't a reliable signal - $WAYLAND_DISPLAY is what every Wayland
// session actually sets, and it's the standard way to tell which of the
// two clipboard mechanisms is live.
bool IsWayland() {
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    return wayland != nullptr && *wayland != '\0';
}

// Writing to a pipe/subprocess stdin whose reader has already gone away
// raises SIGPIPE, which by default terminates the whole process - one bad
// clipboard tool must not be able to take down the tray. Set once,
// process-wide, the first time this file is used (thread-safe via C++11's
// guaranteed-once static-local init); this app never wants the default
// kill-on-broken-pipe behavior anywhere, so there's no need to scope this
// more narrowly than "once, at startup."
void EnsureSigpipeIgnored() {
    static bool once = [] {
        signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)once;
}

// One attempt through the wl-copy -> xclip -> xsel -> file fallback
// chain. Heap-allocated and threaded through as GSubprocess's own
// user_data (see TryNextClipboardCommand/OnClipboardCommandDone), so its
// lifetime is tied entirely to the pending async operation - it holds
// nothing but its own copy of the report text and the remaining commands
// to try, so it stays valid (and harmless to just never finish, if the
// process exits mid-chain) regardless of SniTray's own lifetime.
struct ClipboardAttempt {
    std::string text;
    std::vector<std::string> remainingCommands;
};

// g_subprocess_communicate_async() only ever completes once the child has
// actually exited (GLib docs: "Communicate with the subprocess until it
// terminates, and all input and output has been completed") - correct
// and desired for xclip/xsel, which exit promptly, but wl-copy is
// *designed* to never exit, so waiting for that unconditionally would
// mean the completion callback simply never fires for it: not a hang of
// the main thread (this is still fully async), but the GSubprocess and
// ClipboardAttempt would leak for as long as wl-copy keeps running, i.e.
// indefinitely. A GCancellable tied to a short g_timeout_add() bounds
// this: if the tool hasn't exited within kToolPatienceMs, cancel our own
// wait - which, per GLib's own docs ("Cancelling @cancellable doesn't
// kill the subprocess. Call g_subprocess_force_exit() if it is
// desirable" - confirmed directly against gsubprocess.c before relying
// on it), does NOT touch the child process at all, only abandons this
// process's observation of it. wl-copy keeps running, undisturbed,
// exactly as intended.
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
    // Zeroed before cancelling, not after: g_cancellable_cancel() may
    // invoke OnClipboardCommandDone synchronously, re-entrantly, from
    // within this very call - if that happens, it must see timeoutId==0
    // and skip g_source_remove() on a source that's already in the
    // middle of auto-removing itself via this function's own
    // G_SOURCE_REMOVE return.
    pending->timeoutId = 0;
    g_cancellable_cancel(pending->cancellable);
    return G_SOURCE_REMOVE;
}

void OnClipboardCommandDone(GObject* sourceObject, GAsyncResult* result, gpointer userData) {
    // Reconstructs ownership from the raw pointer handed to
    // g_subprocess_communicate_async() below - safe because GIO
    // guarantees a GAsyncReadyCallback fires exactly once per async call,
    // so this is the single point this pending command is ever freed.
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
        // The tool genuinely exited before our patience window ran out -
        // trust its real, GLib-reported exit status (the same signal
        // pclose() used to give us, for the common case of a tool that
        // does exit promptly).
        treatAsSuccess = g_subprocess_get_successful(subprocess);
    } else {
        // Our own timeout cancelled the wait (or some other wait error
        // occurred) - there's no trustworthy exit status either way.
        // Cancelling never touches the child process itself (see the
        // comment on kToolPatienceMs), so a tool that's still alive
        // after our patience window - the expected, normal outcome for
        // wl-copy - is treated as success: it launched, accepted the
        // data, and hasn't crashed.
        treatAsSuccess = true;
    }
    if (error) {
        g_error_free(error);
    }
    g_object_unref(subprocess);

    std::unique_ptr<ClipboardAttempt> attempt = std::move(pending->attempt);
    delete pending;

    if (treatAsSuccess) {
        return;  // chain ends here
    }
    // Real, verified failure - advance to the next candidate, or the file if none are left.
    TryNextClipboardCommand(std::move(attempt));
}

// Launches the front of attempt->remainingCommands via GSubprocess -
// GLib's own async subprocess API, not hand-rolled fork()/pipe()/
// waitpid(). gio-2.0 is already a hard, REQUIRED build dependency of this
// binary (SniTray.cpp's GDBus-based tray), so this adds no new
// dependency. GSubprocess is GLib's purpose-built answer to exactly this
// problem - spawning a helper process from a GLib-main-loop, GDBus-using
// app without ever blocking the loop or hand-managing SIGCHLD/zombie-
// reaping - which is exactly the category of thing that caused two real
// bugs in this area this session (the SniTray D-Bus startup race, then a
// popen()/pclose() hang specifically on wl-copy - see
// docs/KnownIssues.md).
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
        TryNextClipboardCommand(std::move(attempt));  // try the next candidate, or the file
        return;
    }

    auto* pending = new PendingCommand{std::move(attempt), g_cancellable_new(), 0};
    pending->timeoutId = g_timeout_add(kToolPatienceMs, OnCommandTimeout, pending);

    // stdin_buf is caller-owned per GLib's own docs ("the data is owned
    // by the caller") - g_subprocess_communicate_async() takes its own
    // reference internally, so unref'ing right after the call is
    // correct, not a use-after-free. Verified directly against
    // docs.gtk.org before relying on it.
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
