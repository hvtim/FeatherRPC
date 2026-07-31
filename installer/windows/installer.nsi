; FeatherRPC Windows installer. Builds a single Setup.exe that does what
; install.ps1/uninstall.ps1 already do (per-user install under
; %LOCALAPPDATA%, Start Menu shortcut, optional autostart/desktop shortcut,
; Add/Remove Programs entry) behind a normal installer wizard instead of a
; batch script + PowerShell prompts.
;
; This is a single combined installer for both x64 and arm64 - it embeds
; both architectures' binaries and picks the matching pair at install time
; via x64.nsh's native-CPU detection, so users don't have to pick the right
; download themselves.
;
; Build with makensis (bundled with NSIS: https://nsis.sourceforge.io/):
;
;   makensis /DAPP_DIR_X64="C:\full\path\to\native\build-release-x64\Release" /DAPP_DIR_ARM64="C:\full\path\to\native\build-release-arm64\Release" /DVERSION=0.1.1 installer\windows\installer.nsi
;
; APP_DIR_X64/APP_DIR_ARM64 must each contain the already-built
; FeatherRPC.exe and featherrpc-cli.exe for that architecture, and must be
; absolute paths - NSIS resolves File paths relative to this .nsi file's own
; directory, not the invocation directory, so a relative APP_DIR silently
; resolves to the wrong place. VERSION is used for the output filename
; (FeatherRPC-<VERSION>-windows-installer.exe). Output lands in the repo
; root.

!ifndef APP_DIR_X64
!error "Pass /DAPP_DIR_X64=<path to built x64 FeatherRPC.exe/featherrpc-cli.exe>"
!endif
!ifndef APP_DIR_ARM64
!error "Pass /DAPP_DIR_ARM64=<path to built arm64 FeatherRPC.exe/featherrpc-cli.exe>"
!endif
!ifndef VERSION
!define VERSION "0.0.0"
!endif

!include "MUI2.nsh"
!include "x64.nsh"

Name "FeatherRPC"
OutFile "..\..\FeatherRPC-${VERSION}-windows-installer.exe"
InstallDir "$LOCALAPPDATA\FeatherRPC"
InstallDirRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FeatherRPC" "InstallLocation"
RequestExecutionLevel user
Unicode true
SetCompressor /SOLID lzma

!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\FeatherRPC"

!define MUI_ICON "..\..\assets\icon.ico"
!define MUI_UNICON "..\..\assets\icon.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\FeatherRPC.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start FeatherRPC now"
!define MUI_FINISHPAGE_TEXT "FeatherRPC runs in the system tray - there's no window to open. Once started, it just works in the background; check the tray icon to confirm it's running."
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ; A native exe holds an exclusive file lock while running, so installing
  ; over a running instance (the common case once autostart is on) would
  ; otherwise fail partway through with no clear error.
  nsExec::Exec 'taskkill /IM FeatherRPC.exe /F'
  Pop $0
FunctionEnd

Section "FeatherRPC" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"

  ; Native CPU detection (not RunningX64/IsWow64 - those only tell you
  ; whether the OS is 64-bit, not which 64-bit ISA it natively runs, which
  ; is what decides whether the x64 binary needs emulation on ARM64).
  ${If} ${IsNativeARM64}
    File "${APP_DIR_ARM64}\FeatherRPC.exe"
    File "${APP_DIR_ARM64}\featherrpc-cli.exe"
  ${ElseIf} ${IsNativeAMD64}
    File "${APP_DIR_X64}\FeatherRPC.exe"
    File "${APP_DIR_X64}\featherrpc-cli.exe"
  ${Else}
    Abort "Unsupported CPU architecture - FeatherRPC requires Windows on x64 or ARM64."
  ${EndIf}

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS"
  CreateShortcut "$SMPROGRAMS\FeatherRPC.lnk" "$INSTDIR\FeatherRPC.exe"

  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "FeatherRPC"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "hvtim"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\FeatherRPC.exe"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section "Start at login" SecAutostart
  CreateShortcut "$SMSTARTUP\FeatherRPC.lnk" "$INSTDIR\FeatherRPC.exe"
SectionEnd

Section /o "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\FeatherRPC.lnk" "$INSTDIR\FeatherRPC.exe"
SectionEnd

Section "Uninstall"
  nsExec::Exec 'taskkill /IM FeatherRPC.exe /F'
  Pop $0

  Delete "$INSTDIR\FeatherRPC.exe"
  Delete "$INSTDIR\featherrpc-cli.exe"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\FeatherRPC.lnk"
  Delete "$SMSTARTUP\FeatherRPC.lnk"
  Delete "$DESKTOP\FeatherRPC.lnk"
  DeleteRegKey HKCU "${UNINSTKEY}"
SectionEnd
