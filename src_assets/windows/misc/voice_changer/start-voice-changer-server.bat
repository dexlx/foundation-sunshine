@echo off
REM ===========================================================================
REM Voice Changer Sidecar - Quick Start (identity passthrough mode)
REM ===========================================================================
REM This is the zero-dependency identity (passthrough) mode for verifying
REM that Sunshine -> sidecar IPC works. For real RVC voice changing, run
REM install-voice-changer-deps.bat first, which will overwrite this file
REM with a launcher that uses the bundled Python and RVC backend.
REM ===========================================================================
setlocal
set "SCRIPT_DIR=%~dp0"
set "VC_DIR=%SCRIPT_DIR%..\..\voice_changer"
if not exist "%VC_DIR%\voice_changer_server.py" set "VC_DIR=%SCRIPT_DIR%..\voice_changer"

where python >nul 2>&1
if errorlevel 1 (
    echo [ERROR] python not found in PATH.
    echo Install Python 3.10/3.11 from https://www.python.org/downloads/
    echo or run install-voice-changer-deps.bat to download an embeddable build.
    pause
    exit /b 1
)

cd /d "%VC_DIR%"
echo [*] Starting voice_changer_server (identity passthrough on udp://127.0.0.1:9876)
python voice_changer_server.py --backend identity %*
pause
