#include "ITunesMediaSource.h"
#include "ComHelpers.h"
#include "ComInit.h"

#include <tlhelp32.h>
#include <wchar.h>

namespace platform_windows {

namespace {

bool IsProcessRunning(const wchar_t* processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, processName) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

// Releases an IDispatch* on scope exit - every return path below needs it.
struct ComReleaser {
    IDispatch* ptr;
    ~ComReleaser() {
        if (ptr) {
            ptr->Release();
        }
    }
};

} // namespace

ITunesMediaSource::ITunesMediaSource() {
    EnsureComInitialized();
    _clsidValid = SUCCEEDED(CLSIDFromProgID(L"iTunes.Application", &_clsid));
}

std::optional<core::TrackInfo> ITunesMediaSource::GetCurrentTrack() {
    if (!_clsidValid) {
        return std::nullopt;
    }

    // COM-activating iTunes.Application silently launches iTunes.exe if it
    // isn't already running (standard COM automation behavior) - guard
    // against that so this app can sit idle at login without popping
    // iTunes open on its own.
    if (!IsProcessRunning(L"iTunes.exe")) {
        return std::nullopt;
    }

    IDispatch* rawApp = nullptr;
    if (FAILED(CoCreateInstance(_clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch,
            reinterpret_cast<void**>(&rawApp)))) {
        return std::nullopt;
    }
    ComReleaser appReleaser{rawApp};

    auto playerState = GetDispatchProperty(rawApp, L"PlayerState");
    if (!playerState) {
        return std::nullopt;
    }

    int rawState = VariantToInt(playerState->value);
    core::PlaybackState state = rawState == 1   ? core::PlaybackState::Playing
                                 : rawState == 2 ? core::PlaybackState::Paused
                                                 : core::PlaybackState::Stopped;

    if (state == core::PlaybackState::Stopped) {
        return core::TrackInfo{};
    }

    auto currentTrack = GetDispatchProperty(rawApp, L"CurrentTrack");
    if (!currentTrack || currentTrack->value.vt != VT_DISPATCH || !currentTrack->value.pdispVal) {
        return core::TrackInfo{};
    }
    IDispatch* track = currentTrack->value.pdispVal; // lifetime owned by currentTrack (VariantClear releases it)

    core::TrackInfo info;
    info.state = state;

    if (auto name = GetDispatchProperty(track, L"Name")) info.name = VariantToUtf8String(name->value);
    if (auto artist = GetDispatchProperty(track, L"Artist")) info.artist = VariantToUtf8String(artist->value);
    if (auto album = GetDispatchProperty(track, L"Album")) info.album = VariantToUtf8String(album->value);
    if (auto duration = GetDispatchProperty(track, L"Duration")) info.durationSeconds = VariantToDouble(duration->value);
    if (auto trackNumber = GetDispatchProperty(track, L"TrackNumber")) info.trackNumber = VariantToInt(trackNumber->value);
    if (auto trackCount = GetDispatchProperty(track, L"TrackCount")) info.trackCount = VariantToInt(trackCount->value);
    if (auto position = GetDispatchProperty(rawApp, L"PlayerPosition")) info.elapsedSeconds = VariantToDouble(position->value);

    return info;
}

} // namespace platform_windows
