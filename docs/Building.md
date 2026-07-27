# Building from source

Requires CMake 3.20+ and a C++17 compiler.

```
cd native
cmake -B build -G "Visual Studio 17 2022" -A x64   # or your platform's generator
cmake --build build --config Release
```

Windows also cross-compiles for ARM64 with `-A ARM64` (requires the v143
ARM64 build tools component).

macOS: `cmake -B build -G Xcode && cmake --build build --config Release`
produces a `FeatherRPC.app` bundle. Requires Xcode command line tools.
First configure needs network access - it pulls
[mediaremote-adapter](https://github.com/ungive/mediaremote-adapter) via
`FetchContent` (used by the "any app" media source; see
[KnownIssues.md](KnownIssues.md)) and builds it alongside the app.

Linux: same `cmake -B build && cmake --build build` pattern, generator
picked automatically (Ninja or Makefiles). Requires `gio-2.0`, `dbus-1`,
and `libcurl` development packages.
