@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-presets.ps1" -AppDirectory "%~dp0"
if errorlevel 1 (
  echo Preset installation failed. See the error above.
  pause
  exit /b 1
)
pause
