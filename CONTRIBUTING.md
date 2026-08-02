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
Exit). Click it, then paste the result into the bug report's "Diagnostic
Info" field. It includes: app version/commit, OS, your current config
(including your Discord Application ID, unredacted), a live check of
your selected media source, a tail of the log file, and the crash report
if the app has crashed since it last started.

Check the live media-source line first if FeatherRPC looks "broken" -
the most common cause is the wrong media source selected, not a bug.

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
log rotates at 2MB (keeping 3 backups: `.log.1`, `.log.2`, `.log.3`). For
something intermittent, turn on **Verbose Logging** in the tray's
Settings submenu first and reproduce the issue soon after.

### What a crash report looks like

If FeatherRPC crashes, it writes a report - build version, OS, the
faulting thread/signal, a raw backtrace, and the last few log lines - to
`featherrpc-crash.log`, next to `featherrpc.log`. **Copy Diagnostic Info
folds this in automatically** the next time you open the tray menu.

The backtrace is raw (module names and offsets, not function names) - we
symbolize it offline (see
[Releasing.md](docs/Releasing.md#symbol-archiving)). Just paste it in.

### If the app's own crash handler didn't fire

FeatherRPC's crash handler doesn't catch everything (a hard hang, or a
kill from outside the process). Check the OS's own crash capture:

- **Windows**: Event Viewer → Windows Logs → Application, look for an
  error from "Application Error" around the time it happened.
- **Linux**: `coredumpctl list FeatherRPC` (if `systemd-coredump` is
  installed), or `journalctl -u featherrpc.service` if you're running it
  as the systemd user unit.
- **macOS**: `~/Library/Logs/DiagnosticReports/`, look for a
  `FeatherRPC-<date>.ips` or `.crash` file.

Paste whatever you find from there into the bug report too.

### Submitting

Open a new issue and pick the template that matches - **Bug Report** for
anything that isn't a crash, **Crash Report** if the app actually
stopped running. Both ask for the same diagnostic-info paste; the crash
template also asks about the OS-native crash log above.
