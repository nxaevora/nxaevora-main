from pathlib import Path
p=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c')
s=p.read_text().replace('/* Invalidate the AOT body, including direct/inlined callers. Compile the\n       unchanged guest body first, then dispatch through our wrapper. No guest\n       threads are running yet. Failure leaves the original body to normal JIT. */\n    jit_code_changed(pc, 4);','''/* Equivalent B -> CBZ XZR at the final back edge prevents a hash-matched
       original AOT variant being selected after invalidation. Both instructions
       always branch to pc+0x70. Invalidate direct/inlined AOT callers, compile
       this equivalent body, then install the wrapper before guest threads run.
       Compilation failure leaves normal JIT dispatch without the correction. */
    gw32(pc + 0x194, 0xb4fff6ff);
    jit_code_changed(pc, 0x198);''')
p.write_text(s)
q=Path('outputs/Capture-Arceus-Timing.ps1');s=q.read_text().replace('[switch]$TimingFixes)', '[switch]$TimingFixes, [switch]$EventCameraFix)')
s=s.replace('$env:SNX_ARCEUS_TIMING_FIXES=', "$env:SNX_ARCEUS_EVENT_CAMERA_FIX=if ($EventCameraFix) { '1' } else { '0' }\nif ($EventCameraFix -and !$TimingFixes) { throw 'Event camera correction requires TimingFixes.' }\n$env:SNX_ARCEUS_TIMING_FIXES=")
q.write_text(s)
for fps in (60,120):
 Path(f'outputs/Test-Arceus-Camera-{fps}FPS.cmd').write_text('@echo off\nsetlocal\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Capture-Arceus-Timing.ps1" -ExperimentalFps '+str(fps)+' -TimingFixes -EventCameraFix -CaptureDisplay\npause\n')
Path('work/camera-branch.s').write_text('.text\ncbz xzr, .-0x124\n')
