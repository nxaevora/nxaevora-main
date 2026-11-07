from pathlib import Path
p=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c');s=p.read_text()
Path('work/arceus-clock-before-v3.c').write_text(s)
s=s.replace('#include <stdlib.h>','#include <math.h>\n#include <stdlib.h>')
s=s.replace('static u64 camera_input_calls;','static u64 camera_input_calls;\nstatic int camera_trace_only;\nstatic float camera_speed = 1.0f;')
s=s.replace('    double remaining = (double)ns / 33333334.0;','''    if (camera_trace_only) ns = 33333334;
    double remaining = (double)ns / 33333334.0;
    float raw_yaw = guest_float(object + 0x38c), raw_pitch = guest_float(object + 0x390);
    float deadzone = readable(parameters, 4) ? guest_float(parameters) : 0.0f;
    int active = gr8(object + 0x380) != 0;''')
s=s.replace('''            guest_float_write(object + 0x434, (float)(old_yaw + (yaw - old_yaw) * remaining));
            guest_float_write(object + 0x438, (float)(old_pitch + (pitch - old_pitch) * remaining));''','''            /* The authored release is a linear subtract followed by a clamp.
               Interpolating the already-clamped result leaves an exponential
               tail. Full-stick saturation has the corresponding upper-end bug. */
            float old[2] = {old_yaw, old_pitch}, next[2] = {yaw, pitch};
            float raw[2] = {raw_yaw, raw_pitch};
            for (unsigned axis = 0; axis < 2; axis++) {
                float value = (float)(old[axis] + (next[axis] - old[axis]) * remaining);
                if (!active || fabsf(raw[axis]) < deadzone || raw[axis] * raw[axis] <= 0x1p-23f)
                    value = fmaxf(0.0f, old[axis] - guest_float(object + 0x428 + axis * 8) * (float)remaining);
                else if (fabsf(raw[axis]) == 1.0f && next[axis] >= old[axis])
                    value = fminf(1.0f, old[axis] + guest_float(object + 0x424 + axis * 8) * (float)remaining);
                guest_float_write(object + 0x434 + axis * 4, value);
            }''')
s=s.replace('''LOG("arceus-camera-input: count=%llu step_ns=%llu yaw=%g pitch=%g\\n",
            (unsigned long long)count, (unsigned long long)ns,
            (double)guest_float(object + 0x434), (double)guest_float(object + 0x438));''','''LOG("arceus-camera-input: count=%llu step_ns=%llu yaw=%g pitch=%g ns=%llu object=%llx caller=main+%llx raw=%g,%g deadzone=%g accel=%g,%g decel=%g,%g\\n",
            (unsigned long long)count, (unsigned long long)ns,
            (double)guest_float(object + 0x434), (double)guest_float(object + 0x438),
            (unsigned long long)plat_ns(), (unsigned long long)object,
            (unsigned long long)(return_pc - main_base), (double)raw_yaw, (double)raw_pitch, (double)deadzone,
            (double)guest_float(object + 0x424), (double)guest_float(object + 0x42c),
            (double)guest_float(object + 0x428), (double)guest_float(object + 0x430));''')
s=s.replace('if (!enabled || strcmp(enabled, "1")) return;\n    u64 pc = main_base + 0x193c04c;', 'if ((!enabled || strcmp(enabled, "1")) && !camera_trace_only) return;\n    u64 pc = main_base + 0x193c04c;')
s=s.replace('LOG("arceus-camera-input: fractional acceleration/deceleration experiment installed\\n");','LOG("arceus-camera-input: response hook installed, read_only=%d\\n", camera_trace_only);')
s=s.replace('''c->v[0].f[0] = (float)(authored * ((double)ns / 33333334.0));''','''if (!camera_trace_only) c->v[0].f[0] = (float)(authored * camera_speed * ((double)ns / 33333334.0));''')
s=s.replace('''LOG("arceus-camera-rate: count=%llu step_ns=%llu authored=%g scaled=%g\\n",
            (unsigned long long)count, (unsigned long long)ns,
            (double)authored, (double)c->v[0].f[0]);''','''LOG("arceus-camera-rate: count=%llu step_ns=%llu authored=%g scaled=%g ns=%llu caller=main+%llx speed=%g\\n",
            (unsigned long long)count, (unsigned long long)ns,
            (double)authored, (double)c->v[0].f[0], (unsigned long long)plat_ns(),
            (unsigned long long)(return_pc - main_base), (double)camera_speed);''')
s=s.replace('if (!enabled || strcmp(enabled, "1")) return;\n    u64 pc = main_base + 0x193c2c4;', '''if ((!enabled || strcmp(enabled, "1")) && !camera_trace_only) return;
    const char* speed = getenv("SNX_ARCEUS_CAMERA_SPEED");
    if (speed && *speed) {
        char* end; float value = strtof(speed, &end);
        if (*end || !isfinite(value) || value < 0.25f || value > 4.0f)
            WARN("arceus-camera-rate: invalid camera speed, using 1\\n");
        else camera_speed = value;
    }
    u64 pc = main_base + 0x193c2c4;''')
s=s.replace('LOG("arceus-camera-rate: elapsed-time manual rotation experiment installed\\n");','LOG("arceus-camera-rate: rotation hook installed, read_only=%d speed=%g\\n", camera_trace_only, (double)camera_speed);')
s=s.replace('''void arceus_clock_post_jit(void) {
    const char* enabled''','''void arceus_clock_post_jit(void) {
    const char* probe = getenv("SNX_ARCEUS_CAMERA_TRACE");
    if (main_base && !experimental && probe && !strcmp(probe, "1")) {
        camera_trace_only = 1;
        camera_input_init();
        camera_rate_init();
        return;
    }
    const char* enabled''')
p.write_text(s)
q=Path('outputs/Capture-Arceus-Timing.ps1');s=q.read_text();s=s.replace('[switch]$CameraInputFix)', '[switch]$CameraInputFix, [switch]$CameraTrace, [ValidateRange(0.25,4.0)][double]$CameraSpeed=1.0)')
s=s.replace('$env:SNX_ARCEUS_CAMERA_INPUT_FIX=', '''$env:SNX_ARCEUS_CAMERA_SPEED=$CameraSpeed.ToString([Globalization.CultureInfo]::InvariantCulture)
$env:SNX_ARCEUS_CAMERA_TRACE=if ($CameraTrace) { '1' } else { '0' }
$env:SNX_ARCEUS_CAMERA_INPUT_FIX=''')
s=s.replace('if ($ExperimentalFps) {\n    if (Get-Process', 'if ($ExperimentalFps -or $CameraTrace) {\n    if (Get-Process')
q.write_text(s)
for fps in [60,120]:
 Path(f'outputs/Test-Arceus-Responsive-Camera-{fps}FPS.cmd').write_text('@echo off\nsetlocal\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Capture-Arceus-Timing.ps1" -ExperimentalFps '+str(fps)+' -TimingFixes -EventCameraFix -BlinkFix -CameraInputFix -CameraSpeed 2 -DialogueTrace -CaptureDisplay\npause\n')
Path('outputs/Test-Arceus-Camera-Baseline-30FPS.cmd').write_text('@echo off\nsetlocal\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Capture-Arceus-Timing.ps1" -CameraTrace -CaptureDisplay\npause\n')
