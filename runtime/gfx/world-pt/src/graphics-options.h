#pragma once
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static inline bool arceus_ray_tracing_requested(int configured) {
    const char *value = getenv("ARCEUS_RAY_TRACING");
    return value && *value ? !strcmp(value, "1") : configured != 0;
}

static inline bool arceus_dlss_requested(int configured) {
    const char *value = getenv("ARCEUS_NGX");
    if (value && *value) return atoi(value) != 0;
    value = getenv("ARCEUS_UPSCALER");
    return value && *value ? !strcmp(value, "DLSS") : configured != 0;
}

static inline bool arceus_upscaling_requested(int dlss, int ray_tracing) {
    const char *value = getenv("ARCEUS_UPSCALER");
    return arceus_ray_tracing_requested(ray_tracing) || arceus_dlss_requested(dlss) ||
           (value && !strcmp(value, "FSR"));
}
