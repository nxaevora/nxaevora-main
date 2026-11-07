import sys
sys.path.pop(0)
from pathlib import Path
orig=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/tests/shader_screen_fetch.c').read_text()
helpers=r'''
/* A predicated kill leaves surviving lanes' screen coordinates available across
   an SSY/SYNC join. All words here are synthetic ISA fixtures. */
static u64 flow_branch(u64 base, int from, int to) {
    return field(base, 20, 24, (u32)((to - from - 1) * 8) & 0xffffff);
}
static void flow_base(u64 code[64]) {
    memset(code, 0, 64 * sizeof *code);
    for (int i = 10; i < 64; i++)
        code[i] = (i - 10) % 4 ? 0x50b0000000070f00ull : 0x001f8000fc0007e0ull;
    code[11] = ipa(0, 2); code[12] = ipa(1, 3);
    code[13] = f2i(2, 2); code[15] = f2i(3, 3);
    code[16] = 0xe33000000000000full; /* @P0 KIL */
    code[17] = flow_branch(0xe290000000000000ull, 17, 25);
    code[19] = mov(10, 255);
    code[20] = 0xf0f800000008000full; /* @!P0 SYNC */
    code[21] = mov(10, 255);
    code[23] = 0xf0f800000007000full; /* @PT SYNC */
    code[25] = field(tlds(2, 2, 3), 0, 8, 10);
    code[27] = 0xe30000000007000full;
}
static void flow_test(const char* dir, const char* name, const u64* code, int n, int expect) {
    u64 words[64];
    memcpy(words, code, (size_t)n * sizeof *code);
    for (int i = 0; i < n; i++) words[i] = snx_le64(words[i]);
    u32* spv[2] = {0}; size_t count[2] = {0};
    for (int enabled = 0; enabled < 2; enabled++) {
        ShaderInfo si = {0};
        g_shader_screen_fetch = enabled;
        spv[enabled] = shader_to_spirv((const u8*)words, (u32)n * 8, ST_FS, &si, &count[enabled], 0, 1, 1, 1);
        if (!spv[enabled]) fprintf(stderr, "%s translation: %s\n", name, si.first_unsupported);
    }
    int ok = spv[0] && spv[1];
    if (ok && expect)
        ok = opcount(spv[1], count[1], 169) == opcount(spv[0], count[0], 169) + 2 &&
             opcount(spv[1], count[1], 110) == opcount(spv[0], count[0], 110) + 2 &&
             opcount(spv[1], count[1], 95) == opcount(spv[0], count[0], 95);
    else if (ok) ok = count[0] == count[1] && !memcmp(spv[0], spv[1], count[0] * 4);
    cases++;
    if (!ok) { fprintf(stderr, "FAIL %s (expected change=%d)\n", name, expect); failures++; }
    if (dir && ok) for (int enabled = 0; enabled < 2; enabled++) {
        char path[2048]; snprintf(path, sizeof path, "%s/%s-%d.spv", dir, name, enabled);
        FILE* f = fopen(path, "wb");
        if (!f || fwrite(spv[enabled], 4, count[enabled], f) != count[enabled]) failures++;
        if (f) fclose(f);
    }
    free(spv[0]); free(spv[1]);
}
static void flow_tests(const char* out) {
    u64 code[64];
    flow_base(code); flow_test(out, "flow-conditional-kill", code, 28, 1);
    flow_base(code); code[16] = field(code[16], 16, 4, 8);
    flow_test(out, "flow-inverse-kill", code, 28, 1);
    flow_base(code); code[16] = field(code[16], 16, 4, 7);
    flow_test(out, "flow-unconditional-kill", code, 28, 0);
    flow_base(code); code[16] = 0x50b0000000070f00ull;
    flow_test(out, "flow-ssy-sync-no-kill", code, 28, 1);
    flow_base(code); code[21] = field(mov(2, 255), 16, 4, 0);
    flow_test(out, "flow-predicated-coordinate-write", code, 28, 0);
    flow_base(code); code[11] = flow_branch(0xe24000000000000full, 11, 17);
    code[12] = ipa(0, 2); code[13] = ipa(1, 3); code[15] = f2i(2, 2); code[16] = f2i(3, 3);
    flow_test(out, "flow-skipped-initialization", code, 28, 0);
    flow_base(code); code[27] = 0x50b0000000070f00ull; code[28] = mov(2, 255);
    code[29] = flow_branch(0xe24000000000000full, 29, 25); code[31] = 0xe30000000007000full;
    flow_test(out, "flow-backedge-coordinate-write", code, 32, 0);
}
'''
# Preserve whichever current suite root has; helpers are independent and main call added once.
orig=orig.replace('int main(int argc, char** argv) {',helpers+'\nint main(int argc, char** argv) {')
needle='    g_shader_screen_fetch = 0;\n    printf("screen-fetch:'
assert needle in orig
orig=orig.replace(needle,'    flow_tests(out);\n'+needle)
Path('work/pass8-shader/shader_screen_fetch_flow.c').write_text(orig)
Path('work/pass8-shader/production-flow-helpers.txt').write_text(helpers+'\n/* Call flow_tests(out); near the end of main, before the summary. */\n')
