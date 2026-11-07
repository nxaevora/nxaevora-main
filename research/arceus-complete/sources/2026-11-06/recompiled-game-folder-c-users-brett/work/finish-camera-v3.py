from pathlib import Path
p=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c');s=p.read_text()
s=s.replace('#include "nx/runtime/arceus_step.h"','#include "nx/runtime/arceus_step.h"\n#include "nx/runtime/arceus_camera.h"')
a=s.index('                float value = (float)(old[axis]');z=s.index('                guest_float_write',a)
s=s[:a]+'''                float value = arceus_camera_fraction(old[axis], next[axis], raw[axis], deadzone, active,
                    guest_float(object + 0x424 + axis * 8), guest_float(object + 0x428 + axis * 8), remaining);
'''+s[z:]
s=s.replace('count <= 8 || count % 600 == 0','count <= 8 || count % 60 == 0')
s=s.replace('static u64 camera_rate_calls;', 'static u64 camera_rate_calls;\nstatic float camera_last_authored = -1.0f;')
s=s.replace('''    u64 return_pc = c->x[30];
    camera_rate_original''','''    u64 return_pc = c->x[30], camera_object = c->x[19];
    camera_rate_original''')
s=s.replace('''    if (count <= 8 || count % 60 == 0)
        LOG("arceus-camera-rate:''','''    int changed = authored != camera_last_authored;
    camera_last_authored = authored;
    if (changed || count <= 8 || count % 60 == 0) {
        LOG("arceus-camera-rate:''')
s=s.replace('''(unsigned long long)(return_pc - main_base), (double)camera_speed);
}''','''(unsigned long long)(return_pc - main_base), (double)camera_speed);
        if (return_pc == main_base + 0x193d270 && readable(camera_object, 0x70c))
            LOG("arceus-camera-field: object=%llx yaw=%g yaw_step=%g pitch_step=%g\\n",
                (unsigned long long)camera_object, (double)guest_float(camera_object + 0x360),
                (double)guest_float(camera_object + 0x420), (double)guest_float(camera_object + 0x708));
    }
}''')
p.write_text(s)
Path(p.parent/'arceus_camera.h').write_text('''/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_ARCEUS_CAMERA_H
#define SNX_ARCEUS_CAMERA_H
#include <math.h>
/* Used only for the fractional last update (0 <= fraction < 1).
   Preserve the game's deadzone/curve result for partial stick input. At zero
   and full stick, interpolate the unclamped linear update, then clamp. */
static inline float arceus_camera_fraction(float old, float next, float raw,
    float deadzone, int active, float accel, float decel, double fraction) {
    if (!active || fabsf(raw) < deadzone || raw * raw <= 0x1p-23f)
        return fmaxf(0.0f, old - decel * (float)fraction);
    if (fabsf(raw) == 1.0f && next >= old)
        return fminf(1.0f, old + accel * (float)fraction);
    return (float)(old + (next - old) * fraction);
}
#endif
''')
