#include "ShellLinkAutoLaunch.h"
#include "ComInit.h"

#include "core/AppConfig.h"
#include "core/ConfigPaths.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <filesystem>

namespace platform_windows {

namespace {

std::filesystem::path ShortcutPath(const std::wstring& name) {
    PWSTR path = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &path))) {
        result = std::filesystem::path(path) / name;
    }
    if (path) {
        CoTaskMemFree(path);
    }
    return result;
}

std::filesystem::path SelfExePath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    return exePath;
}

} // namespace

ShellLinkAutoLaunch::ShellLinkAutoLaunch(std::filesystem::path targetExePath, std::wstring shortcutName)
    : _targetExePath(std::move(targetExePath)), _shortcutName(std::move(shortcutName)) {}

bool ShellLinkAutoLaunch::IsEnabled() const {
    auto path = ShortcutPath(_shortcutName);
    return !path.empty() && std::filesystem::exists(path);
}

void ShellLinkAutoLaunch::SetEnabled(bool enabled) {
    auto path = ShortcutPath(_shortcutName);
    if (path.empty()) {
        return;
    }

    if (!enabled) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return;
    }

    EnsureComInitialized();

    std::filesystem::path exePath = _targetExePath.empty() ? SelfExePath() : _targetExePath;
    std::filesystem::path installDir = exePath.parent_path();

    // Reflects the current tray preference in the shortcut's own
    // arguments rather than needing a separate shortcut per mode -
    // `featherrpc tray on/off` re-runs SetEnabled(true) to keep this in
    // sync whenever autostart is already registered.
    core::AppConfig config = core::LoadConfig(core::GetConfigFilePath());
    std::wstring arguments = config.trayEnabled ? L"" : L"--no-tray";

    IShellLinkW* shellLink = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
            reinterpret_cast<void**>(&shellLink)))) {
        return;
    }

    shellLink->SetPath(exePath.c_str());
    shellLink->SetArguments(arguments.c_str());
    shellLink->SetWorkingDirectory(installDir.c_str());
    shellLink->SetDescription(L"Now-playing sync for Discord Rich Presence");

    IPersistFile* persistFile = nullptr;
    if (SUCCEEDED(shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile)))) {
        persistFile->Save(path.c_str(), TRUE);
        persistFile->Release();
    }

    shellLink->Release();
}

} // namespace platform_windows
