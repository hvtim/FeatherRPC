@echo off
choice /C YN /M "Start FeatherRPC automatically when you log in"
set AUTOSTART=%errorlevel%

choice /C YN /M "Add a Desktop shortcut"
set DESKTOP=%errorlevel%

set ARGS=
if "%AUTOSTART%"=="2" set ARGS=%ARGS% -NoAutoStart
if "%DESKTOP%"=="1" set ARGS=%ARGS% -Desktop

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %ARGS%
echo.
pause
