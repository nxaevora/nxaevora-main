@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Capture-Arceus-Timing.ps1" -CameraTrace -TimingResearch -CaptureDisplay
pause
