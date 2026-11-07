/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_ARCEUS_CAMERA_H
#define SNX_ARCEUS_CAMERA_H
#include <math.h>
/* Used only for the fractional last update (0 <= fraction < 1).
   Preserve the game's deadzone/curve result for partial stick input. At zero
   and full stick, interpolate the unclamped linear update, then clamp. */
static inline float arceus_camera_fraction(float old, float next, float raw,
    float deadzone, int active, float accel, float decel, double fraction) {
    if (!active || (deadzone > 0.0f && deadzone < 1.0f && fabsf(raw) < deadzone) || raw * raw <= 0x1p-23f)
        return fmaxf(0.0f, old - decel * (float)fraction);
    if (fabsf(raw) == 1.0f && next == fminf(1.0f, old + accel))
        return fminf(1.0f, old + accel * (float)fraction);
    return (float)(old + (next - old) * fraction);
}
#endif
