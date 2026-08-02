param(
    [switch]$NoAutoStart,
    [switch]$Desktop,
    [switch]$NoTray
)

$ErrorActionPreference = "Stop"

# Shown in Windows' "Installed apps" list. The release workflow patches
# this bundled copy's literal to the real release version at build time -
# this checked-in value only matters for an ad-hoc manual/from-source run.
$appVersion = "0.1.0"
$exeName = "FeatherRPC.exe"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceDir = Join-Path $scriptDir "app"
$installDir = Join-Path $env:LOCALAPPDATA "FeatherRPC"

if (!(Test-Path $sourceDir)) {
    Write-Host "Could not find the app files next to this script (expected '$sourceDir')." -ForegroundColor Red
    Write-Host "Make sure you extracted the whole release zip before running install.bat."
    exit 1
}

Write-Host "Installing FeatherRPC to $installDir ..."
New-Item -ItemType Directory -Force -Path $installDir | Out-Null

# Stop any already-running instance before copying files over it - a native
# exe holds an exclusive file lock while running (unlike .NET's more
# lenient assembly loading), so Copy-Item below would fail on any upgrade
# where the app is already running, which is the common case once autostart
# is enabled.
Get-Process ([System.IO.Path]::GetFileNameWithoutExtension($exeName)) -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

# One-time migration cleanup: the old .NET build shipped these dependency
# files alongside FeatherRPC.exe/.dll - the native build needs none of them,
# but Copy-Item below only overwrites matching filenames, so they'd
# otherwise be left behind as dead weight (Microsoft.Windows.SDK.NET.dll
# alone is ~25MB). Safe to remove this list once most installs have
# upgraded past the native rewrite.
$staleDotNetFiles = @(
    "FeatherRPC.dll",
    "FeatherRPC.pdb",
    "FeatherRPC.deps.json",
    "FeatherRPC.runtimeconfig.json",
    "WinRT.Runtime.dll",
    "Microsoft.Windows.SDK.NET.dll"
)
foreach ($staleFile in $staleDotNetFiles) {
    $staleFilePath = Join-Path $installDir $staleFile
    if (Test-Path $staleFilePath) {
        Remove-Item $staleFilePath -Force -ErrorAction SilentlyContinue
    }
}

Copy-Item -Path (Join-Path $sourceDir "*") -Destination $installDir -Recurse -Force

# Copy the uninstaller into the install directory too, so Windows' "Installed
# apps" entry (and its UninstallString) keeps working even if the user deletes
# the original downloaded/extracted release folder.
Copy-Item -Path (Join-Path $scriptDir "uninstall.ps1") -Destination $installDir -Force
Copy-Item -Path (Join-Path $scriptDir "uninstall.bat") -Destination $installDir -Force

$exePath = Join-Path $installDir $exeName
$shell = New-Object -ComObject WScript.Shell

function New-AppShortcut($path) {
    $shortcut = $shell.CreateShortcut($path)
    $shortcut.TargetPath = $exePath
    # All shortcuts (not just Startup) launch headless when -NoTray was
    # requested - otherwise double-clicking the Start Menu entry would
    # unexpectedly pop a tray icon the user explicitly opted out of.
    if ($NoTray) {
        $shortcut.Arguments = "--no-tray"
    }
    $shortcut.WorkingDirectory = $installDir
    $shortcut.Description = "Now-playing sync for Discord Rich Presence"
    $shortcut.Save()
}

# Start Menu entry is always created - without it, there was no way to relaunch
# the app after quitting it except navigating to the install directory by hand.
$startMenuDir = [Environment]::GetFolderPath("StartMenu")
$startMenuDir = Join-Path $startMenuDir "Programs"
New-AppShortcut (Join-Path $startMenuDir "FeatherRPC.lnk")

if (-not $NoAutoStart) {
    $startupDir = [Environment]::GetFolderPath("Startup")
    New-AppShortcut (Join-Path $startupDir "FeatherRPC.lnk")
}

if ($Desktop) {
    $desktopDir = [Environment]::GetFolderPath("Desktop")
    New-AppShortcut (Join-Path $desktopDir "FeatherRPC.lnk")
}

# Register in Windows' "Installed apps" (Settings > Apps), per-user, no admin
# required, so it can be uninstalled from there even without the release zip.
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\FeatherRPC"
New-Item -Path $uninstallKey -Force | Out-Null
$installedSizeKb = [math]::Round((Get-ChildItem $installDir -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1KB)
Set-ItemProperty -Path $uninstallKey -Name "DisplayName" -Value "FeatherRPC"
Set-ItemProperty -Path $uninstallKey -Name "DisplayVersion" -Value $appVersion
Set-ItemProperty -Path $uninstallKey -Name "Publisher" -Value "hvtim"
Set-ItemProperty -Path $uninstallKey -Name "DisplayIcon" -Value $exePath
Set-ItemProperty -Path $uninstallKey -Name "InstallLocation" -Value $installDir
Set-ItemProperty -Path $uninstallKey -Name "UninstallString" -Value "powershell.exe -WindowStyle Hidden -NoProfile -ExecutionPolicy Bypass -File `"$installDir\uninstall.ps1`""
Set-ItemProperty -Path $uninstallKey -Name "NoModify" -Value 1 -Type DWord
Set-ItemProperty -Path $uninstallKey -Name "NoRepair" -Value 1 -Type DWord
Set-ItemProperty -Path $uninstallKey -Name "EstimatedSize" -Value $installedSizeKb -Type DWord

if ($NoAutoStart) {
    Write-Host "Installed. Autostart at login was skipped - enable it anytime from the tray menu." -ForegroundColor Green
} else {
    Write-Host "Installed. FeatherRPC will now start automatically at login." -ForegroundColor Green
}
Write-Host "Added a Start Menu shortcut$(if ($Desktop) { ' and a Desktop shortcut' })."
Write-Host "It also now shows up in Windows Settings > Apps > Installed apps."
Write-Host ""

Get-Process ([System.IO.Path]::GetFileNameWithoutExtension($exeName)) -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

if ($NoTray) {
    Start-Process -FilePath $exePath -ArgumentList "--no-tray" -WorkingDirectory $installDir
    Write-Host "Started FeatherRPC headless (no tray icon)." -ForegroundColor Green
    Write-Host "Configure it with featherrpc-cli.exe, e.g.: featherrpc-cli appid set <your-discord-app-id>"
} else {
    Start-Process -FilePath $exePath -WorkingDirectory $installDir
    Write-Host "Started FeatherRPC. A tray icon should now appear - right-click it to enter your Discord Application ID." -ForegroundColor Green
}
