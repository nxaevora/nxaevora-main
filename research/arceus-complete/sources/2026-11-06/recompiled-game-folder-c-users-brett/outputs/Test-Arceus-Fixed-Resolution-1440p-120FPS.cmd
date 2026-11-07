@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Capture-Arceus-Timing.ps1" -ExperimentalFps 120 -TimingFixes -EventCameraFix -BlinkFix -CameraInputFix -CameraSpeed 2.2 -MenuRepeatFix -DialogueTrace -TimingResearch -FixedSceneResolution -ResolutionTrace -ResolutionScale 1.3333333333333333 -Fullscreen -CaptureDisplay
pause
