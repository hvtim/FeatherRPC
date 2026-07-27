; FeatherRPC Windows installer. Builds a single Setup.exe that does what
; install.ps1/uninstall.ps1 already do (per-user install under
; %LOCALAPPDATA%, Start Menu shortcut, optional autostart/desktop shortcut,
; Add/Remove Programs entry) behind a normal installer wizard instead of a
; batch script + PowerShell prompts.
;
; Build with makensis (bundled with NSIS: https://nsis.sourceforge.io/):
;
;   makensis /DAPP_DIR="C:\full\path\to\native\build-release-x64\Release" /DVERSION=0.1.1 /DARCH=x64 installer\windows\installer.nsi
;
; APP_DIR must contain the already-built FeatherRPC.exe and
; featherrpc-cli.exe, and must be an absolute path - NSIS resolves File
; paths relative to this .nsi file's own directory, not the invocation
; directory, so a relative APP_DIR silently resolves to the wrong place.
; VERSION and ARCH are used for the output filename
; (FeatherRPC-<VERSION>-Setup-<ARCH>.exe); the same script builds both x64
; and arm64 installers. Output lands in the repo root.

!ifndef APP_DIR
!error "Pass /DAPP_DIR=<path to built FeatherRPC.exe/featherrpc-cli.exe>"
!endif
!ifndef VERSION
!define VERSION "0.0.0"
!endif
!ifndef ARCH
!define ARCH "x64"
!endif

!include "MUI2.nsh"

Name "FeatherRPC"
OutFile "..\..\FeatherRPC-${VERSION}-Setup-${ARCH}.exe"
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
  File "${APP_DIR}\FeatherRPC.exe"
  File "${APP_DIR}\featherrpc-cli.exe"
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
