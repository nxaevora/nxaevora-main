@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Capture-Arceus-Timing.ps1" -ExperimentalFps 120 -TimingFixes -EventCameraFix -BlinkFix -CameraInputFix -CameraSpeed 2.2 -MenuRepeatFix -DialogueTrace -TimingResearch -ResolutionTrace -ResolutionScale 1 -Fullscreen
pause
