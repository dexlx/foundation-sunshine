@echo off
REM ===========================================================================
REM Voice Changer Sidecar - One-Click Dependency Installer
REM ===========================================================================
REM Installs Python 3.11 (if missing) + RVC backend dependencies.
REM
REM Usage:
REM   double-click, OR
REM   install-voice-changer-deps.bat            (use system Python if 3.10/3.11)
REM   install-voice-changer-deps.bat --embed    (download embeddable Python)
REM ===========================================================================
setlocal EnableDelayedExpansion
set "SCRIPT_DIR=%~dp0"
set "VC_DIR=%SCRIPT_DIR%..\voice_changer"
if not exist "%VC_DIR%" set "VC_DIR=%SCRIPT_DIR%"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-voice-changer-deps.ps1" %*
if errorlevel 1 (
    echo.
    echo [ERROR] Installation failed. See messages above.
    pause
    exit /b 1
)
echo.
echo [OK] Voice changer dependencies installed.
echo You can now start the sidecar: start-voice-changer-server.bat
pause
