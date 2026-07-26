# CLI Reference

Every platform ships a small command-line control tool alongside the main
app. It edits `config.json` directly, and if an instance - tray or
headless - is already running, tells it to reload live instead of waiting
for the next launch.

## Binary name

- Windows: `featherrpc-cli.exe`
- macOS: `featherrpc-cli`
- Linux: `featherrpc` (Linux filesystems are case-sensitive, so this
  doesn't collide with the `FeatherRPC` tray binary the way it would on
  Windows/macOS)

Run it with no arguments to print the full command list.

## Commands

### `appid get` / `appid set <id>`

Get or set the Discord Application ID used for Rich Presence. Create one
at [discord.com/developers/applications](https://discord.com/developers/applications)
and use its Application ID here.

### `broadcast get` / `broadcast on` / `broadcast off`

Get or set whether presence is sent to Discord at all. Off pauses updates
without closing the app.

### `tracknumber get` / `tracknumber on` / `tracknumber off`

Get or set whether the track number is included in the presence text, for
sources that report one.

### `artmode get` / `artmode auto` / `artmode custom` / `artmode off`

Get or set how cover art is chosen:

- `auto` - look up cover art automatically (Apple's iTunes Search API).
  Falls back to the `icon` image if no match is found.
- `custom` - use the URL set by `arturl set`. Falls back to automatic
  lookup if no URL has been set.
- `off` - always use the `icon` image, never look anything up.

See [AlbumArt.md](AlbumArt.md) for how each mode actually resolves an
image.

### `arturl get` / `arturl set <url>`

Get or set the image URL used when `artmode` is `custom`. Ignored in any
other art mode.

### `icon get` / `icon set <key>`

Get or set the Rich Presence asset key used as the static image (when
`artmode` is `off`) and as the automatic-lookup fallback. Must be an asset
key already uploaded to your Discord application's Rich Presence Art
Assets, not an arbitrary image or URL - see
[AlbumArt.md](AlbumArt.md#the-fallback-image). Also settable from the
tray menu's Album Art > "Set Fallback Image Key..." prompt.

### `pollinterval get` / `pollinterval set <ms>`

Get or set how often the media source is polled, in milliseconds. Must be
a positive number.

### `mediasource list` / `mediasource get` / `mediasource set <id>`

List the media sources currently available to pick from, get the
currently selected one, or set it.

- `list` prints each source's id and display name, one per line. Not
  available on macOS - Music.app is the only source there, nothing to
  list.
- Windows: `id` is `iTunes` (COM automation) or an SMTC app user model id
  (e.g. `vlc.exe`) for any other app reporting now-playing info to
  Windows.
- Linux: `id` is an MPRIS bus name (e.g.
  `org.mpris.MediaPlayer2.plasma-browser-integration`).
- macOS: fixed to Music.app; there's nothing else to point `set` at yet.

### `tray get` / `tray on` / `tray off`

Get or set whether the app shows a tray icon. Takes effect on the next
launch only - a running instance can't add or remove its own tray icon
mid-session.

### `status`

Print what the running instance is currently showing on Discord, or
`Not running.` if nothing's running.

### `autostart get` / `autostart on` / `autostart off`

Get or set whether the app launches automatically at login.

- Windows: a Startup shortcut.
- macOS: a LaunchAgent.
- Linux: a systemd **user** unit that runs the app headless (`--no-tray`).
  This is separate from the tray's own "Start at login" toggle (an XDG
  autostart entry, set from the tray menu) - the two don't affect each
  other. Manage the unit directly with
  `systemctl --user status featherrpc.service` if needed.

### `daemon start` / `daemon stop` / `daemon restart`

Start, stop, or restart a headless (`--no-tray`) instance directly,
without going through systemd/LaunchAgent/Startup. Useful for testing
without touching autostart. This is independent of any tray instance you
may already have running - starting a daemon while a tray instance is
running gives you two separate instances, not one restarted instance.

### `config path`

Print the full path to `config.json`.

## Live vs queued changes

Every `set`/`on`/`off` command reports one of:

- `Saved - applied live.` - a running instance (tray or headless) picked
  the change up immediately, no restart needed.
- `Saved - not running, will apply next start.` - nothing was running to
  signal; the change is queued in `config.json` for whenever the app next
  starts.

`tray on`/`tray off` is the one exception - it always prints a fixed
message instead, since a running instance can never apply that one live
(see `tray` above).
