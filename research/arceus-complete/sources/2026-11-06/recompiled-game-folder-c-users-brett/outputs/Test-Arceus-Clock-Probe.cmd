@echo off
setlocal
set SNX_ARCEUS_CLOCK_TRACE=1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Capture-Arceus-Timing.ps1"
pause
