@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Capture-Arceus-Timing.ps1" -ExperimentalFps 60 -TimingFixes -EventCameraFix -BlinkFix -CameraInputFix -DialogueTrace -CaptureDisplay
pause
