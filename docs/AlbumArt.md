# Album Art

FeatherRPC shows an image next to the track in Discord's Rich Presence.
Where that image comes from depends on the art mode.

## Art modes

- **Auto** (default) - looks up cover art automatically, by
  artist/title/album. Falls back to the fallback image (below) if nothing
  matches.
- **Custom** - always uses a single image URL you set yourself. Falls
  back to automatic lookup if no URL has been set.
- **Off** - always uses the fallback image, never looks anything up.

Set the mode with `artmode auto|custom|off` (see
[CLI.md](CLI.md#artmode-get--artmode-auto--artmode-custom--artmode-off)),
or from the tray menu.

## Automatic lookup

Two sources are tried in order, so a miss on the first doesn't mean no
art at all:

1. **Apple's iTunes Search API** - tried first, by album then by track.
   No API key, generally the fastest and most complete for mainstream
   releases.
2. **MusicBrainz + the Cover Art Archive** - tried only if iTunes finds
   nothing. MusicBrainz is queried for a matching release (by album, then
   by track), and if found, the Cover Art Archive is checked for that
   release's front cover.

If both miss, FeatherRPC falls back to the fallback image.

## The fallback image

Automatic lookup can miss - an obscure track, a browser tab title with no
real metadata, or the source not reporting an album at all. `Off` mode
skips lookup entirely. Either way, FeatherRPC needs something to show, and
that something isn't a plain image file bundled with the app - it's a
**Rich Presence Art Asset**, a named image uploaded to your own Discord
application.

To set one up:

1. Go to your application at
   [discord.com/developers/applications](https://discord.com/developers/applications).
2. Rich Presence > Art Assets > upload an image.
3. Name it exactly `fallback` (the default FeatherRPC looks for).

If you'd rather use a different name - or you already have an asset under
another name from before - point FeatherRPC at it via the tray menu's
Album Art > "Set Fallback Image Key..." prompt, or the CLI:

```
featherrpc icon set <your-asset-name>
```

(`featherrpc-cli` on Windows/macOS - see [CLI.md](CLI.md).) Check the
current value with `icon get`.

Whatever name is set here must match an asset already uploaded under that
exact name in the Developer Portal - it's not an arbitrary image or URL,
and there's no fallback-for-the-fallback if the name doesn't match
anything uploaded (Discord just shows no image).

## Custom art URL

In `Custom` mode, FeatherRPC uses a single image URL you provide directly
- no Developer Portal upload needed, since this one goes straight into
the Rich Presence payload as an external URL rather than an asset key.
Set it with `arturl set <url>` or the tray menu's "Custom image URL..."
prompt.
