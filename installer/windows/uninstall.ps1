$ErrorActionPreference = "SilentlyContinue"

$exeName = "FeatherRPC.exe"

Write-Host "Uninstalling FeatherRPC..."

Get-Process ([System.IO.Path]::GetFileNameWithoutExtension($exeName)) -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

$startupDir = [Environment]::GetFolderPath("Startup")
$shortcutPath = Join-Path $startupDir "FeatherRPC.lnk"
if (Test-Path $shortcutPath) {
    Remove-Item $shortcutPath -Force
    Write-Host "Removed autostart shortcut."
}

$startMenuDir = Join-Path ([Environment]::GetFolderPath("StartMenu")) "Programs"
$startMenuShortcut = Join-Path $startMenuDir "FeatherRPC.lnk"
if (Test-Path $startMenuShortcut) {
    Remove-Item $startMenuShortcut -Force
    Write-Host "Removed Start Menu shortcut."
}

$desktopShortcut = Join-Path ([Environment]::GetFolderPath("Desktop")) "FeatherRPC.lnk"
if (Test-Path $desktopShortcut) {
    Remove-Item $desktopShortcut -Force
    Write-Host "Removed Desktop shortcut."
}

$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\FeatherRPC"
if (Test-Path $uninstallKey) {
    Remove-Item $uninstallKey -Recurse -Force
    Write-Host "Removed entry from Windows' Installed apps list."
}

$installDir = Join-Path $env:LOCALAPPDATA "FeatherRPC"
if (Test-Path $installDir) {
    # This script is also shipped inside $installDir itself (so the registered
    # UninstallString keeps working without the original release folder), which
    # means it may be running from the very directory it needs to delete.
    # Deleting it directly here can fail with a file-in-use error, so hand the
    # delete off to a short-lived detached process that waits for this one to
    # fully exit first.
    Start-Process cmd.exe -ArgumentList "/c timeout /t 2 /nobreak >nul & rmdir /s /q `"$installDir`"" -WindowStyle Hidden
    Write-Host "Removing installed files from $installDir..."
}

Write-Host ""
Write-Host "FeatherRPC has been fully uninstalled." -ForegroundColor Green
Write-Host "Nothing else on this machine was changed - Discord and iTunes themselves are untouched."
