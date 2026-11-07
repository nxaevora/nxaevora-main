/* Optional local shader capture for the forward reflection linker. */
static bool capture_forward_hash(u64 hash) {
#ifdef FORWARD_MODULE_CAPTURE
    return world_reflection_program(hash);
#else
    (void)hash;
    return false;
#endif
}

static void capture_forward_module(u64 hash, const u32 *words, size_t count) {
    const char *directory = getenv("ARCEUS_SHADER_CAPTURE_DIR");
    if (!directory || !*directory) return;
    u64 digest = 14695981039346656037ull;
    for (size_t i = 0; i < count; ++i) digest = (digest ^ words[i]) * 1099511628211ull;
    char path[1024];
    int length = snprintf(path, sizeof path, "%s/%016llx-%016llx.spv", directory,
                          (unsigned long long)hash, (unsigned long long)digest);
    if (length < 0 || (size_t)length >= sizeof path) return;
    FILE *file = fopen(path, "wb");
    if (file) {
        fwrite(words, sizeof *words, count, file);
        fclose(file);
    }
}
