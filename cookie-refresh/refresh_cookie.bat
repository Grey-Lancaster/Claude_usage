@echo off
rem Double-click launcher for Windows - no PowerShell/bash syntax to get
rem wrong, no need to remember "pip install -r requirements.txt" or "cd"
rem into this folder first. Installs dependencies on first run (or after
rem requirements.txt changes), then just runs the script every time after.
cd /d "%~dp0"

where python >nul 2>nul
if errorlevel 1 (
    echo Python wasn't found on PATH. Install it from https://python.org/downloads/
    echo ^(check "Add python.exe to PATH" in the installer^), then run this again.
    pause
    exit /b 1
)

python -c "import requests" >nul 2>nul
if errorlevel 1 (
    echo First run - installing dependencies...
    pip install -r requirements.txt
    if errorlevel 1 (
        echo Dependency install failed - see the error above.
        pause
        exit /b 1
    )
)

python refresh_cookie.py %*

echo.
pause
