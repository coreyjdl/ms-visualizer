@echo off
cd /d "%~dp0"
if not exist "%~dp0MS-Visualizer.exe" (
  echo MS-Visualizer.exe is missing. Extract the complete release ZIP first.
  pause
  exit /b 1
)
start "" "%~dp0MS-Visualizer.exe"
