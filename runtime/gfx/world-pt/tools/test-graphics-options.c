#include "../src/graphics-options.h"
#include <assert.h>
#include <stdio.h>

static void option(const char *name, const char *value) {
#ifdef _WIN32
    assert(_putenv_s(name, value) == 0);
#else
    assert(setenv(name, value, 1) == 0);
#endif
}

int main(void) {
    option("ARCEUS_NGX", "");
    option("ARCEUS_UPSCALER", "");
    option("ARCEUS_RAY_TRACING", "");
    assert(!arceus_dlss_requested(0));
    assert(!arceus_upscaling_requested(0, 0));
    assert(!arceus_ray_tracing_requested(0));
    option("ARCEUS_UPSCALER", "DLSS");
    assert(arceus_dlss_requested(0) && arceus_upscaling_requested(0, 0));
    assert(!arceus_ray_tracing_requested(0));
    option("ARCEUS_NGX", "0");
    assert(!arceus_dlss_requested(0) && !arceus_upscaling_requested(0, 0));
    option("ARCEUS_UPSCALER", "FSR");
    assert(arceus_upscaling_requested(0, 0) && !arceus_dlss_requested(0));
    option("ARCEUS_RAY_TRACING", "1");
    assert(arceus_ray_tracing_requested(0) && !arceus_dlss_requested(0));
    option("ARCEUS_UPSCALER", "Native");
    assert(arceus_upscaling_requested(0, 0));
    option("ARCEUS_RAY_TRACING", "0");
    assert(!arceus_ray_tracing_requested(0) && !arceus_upscaling_requested(0, 0));
    option("ARCEUS_NGX", "1");
    assert(arceus_dlss_requested(0) && !arceus_ray_tracing_requested(0));
    option("ARCEUS_NGX", "");
    option("ARCEUS_UPSCALER", "");
    option("ARCEUS_RAY_TRACING", "");
    assert(arceus_dlss_requested(1) && !arceus_dlss_requested(0));
    assert(arceus_ray_tracing_requested(1) && !arceus_ray_tracing_requested(0));
    assert(arceus_upscaling_requested(1, 0) && arceus_upscaling_requested(0, 1));
    option("ARCEUS_NGX", "0");
    option("ARCEUS_RAY_TRACING", "0");
    assert(!arceus_dlss_requested(1) && !arceus_ray_tracing_requested(1));
    puts("PASS: native defaults and independent DLSS, FSR and ray tracing options");
    return 0;
}
