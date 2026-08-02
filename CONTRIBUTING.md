# Contributing

## Reporting Bugs

### Before filing

- Confirm you're on the latest release - `featherrpc-cli`/`featherrpc`
  don't self-update, and an old build may already have your issue fixed.
- Check [KnownIssues.md](docs/KnownIssues.md) - it documents open
  limitations, deliberate scope limits, and already-fixed bugs. Yours may
  already be explained there.
- Search existing issues first, so we're not tracking the same report
  twice.

### Producing a diagnostic report

The tray's right-click menu has a **Copy Diagnostic Info** action (near
Exit). Click it, then paste the result directly into the bug report's
"Diagnostic Info" field - that one paste is almost always everything we
need: app version/commit, OS, your current config (poll interval, media
source, art mode, etc. - your Discord Application ID is included
unredacted, since it's a public identifier, not a secret), a live check of
whether your selected media source is actually reachable right now, a tail
of the log file, and - if the app has crashed since it was last
started - the crash report from that too.

That live media-source check is worth calling out on its own: if
FeatherRPC looks "broken" because nothing's showing up in Discord, the
most common cause by far is that the wrong media source is selected (or
none at all) - not an actual bug. The diagnostic report states plainly
whether your selected source currently has an active session, and lists
what's actually available, so this is usually visible immediately without
needing to dig through the log by hand.

### Log file location

If you need the raw log file instead of (or in addition to) Copy
Diagnostic Info:

| Platform | Path |
|---|---|
| Windows | `%LOCALAPPDATA%\FeatherRPC\featherrpc.log` |
| Linux | `$XDG_CONFIG_HOME/FeatherRPC/featherrpc.log`, or `~/.config/FeatherRPC/featherrpc.log` if `XDG_CONFIG_HOME` isn't set |
| macOS | `~/Library/Application Support/FeatherRPC/featherrpc.log` |

`featherrpc-cli log path` / `featherrpc log path` prints the exact path
for your machine, and `config path` does the same for `config.json`. The
log rotates at 2MB (keeping 3 backups: `.log.1`, `.log.2`, `.log.3`), so it
won't grow without bound, but also won't hold months of history - if
you're chasing something intermittent, turn on **Verbose Logging** in the
tray's Settings submenu first and reproduce the issue soon after.

### What a crash report looks like

If FeatherRPC crashes, it writes a best-effort report - build
version, OS, the faulting thread/signal, a raw backtrace, and the last few
log lines before the crash - to a separate file, `featherrpc-crash.log`,
right next to `featherrpc.log`. **Copy Diagnostic Info already folds this
in automatically** the next time you open the tray menu, with a note if
it looks like it might be from an older, unrelated session - you don't
need to go find this file yourself in the normal case.

The backtrace in that report is intentionally raw (module names and
offsets, not function names) - we symbolize it ourselves offline against
the exact matching release build's debug symbols (see
[Releasing.md](docs/Releasing.md#symbol-archiving)) once you've filed the
issue. You don't need to do anything to make it useful beyond pasting it
in.

### If the app's own crash handler didn't fire

FeatherRPC's crash handler catches the most common fatal signals, but not
every possible way a process can die (e.g. a hard hang, or a kill from
outside the process). The OS's own crash capture is a good fallback in
that case:

- **Windows**: Event Viewer → Windows Logs → Application, look for an
  error from "Application Error" around the time it happened.
- **Linux**: `coredumpctl list FeatherRPC` (if `systemd-coredump` is
  installed), or `journalctl -u featherrpc.service` if you're running it
  as the systemd user unit.
- **macOS**: `~/Library/Logs/DiagnosticReports/`, look for a
  `FeatherRPC-<date>.ips` or `.crash` file.

Paste whatever you find from there into the bug report too - it's useful
supplementary information even when it's not a full substitute for our
own crash report.

### Submitting

Open a new issue and pick the template that matches - **Bug Report** for
anything that isn't a crash, **Crash Report** if the app actually
stopped running. Both ask for the same diagnostic-info paste; the crash
template also asks about the OS-native crash log above.
