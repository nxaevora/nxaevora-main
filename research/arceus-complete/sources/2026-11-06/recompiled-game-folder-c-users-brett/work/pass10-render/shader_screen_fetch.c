/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Opt-in current-pixel fetches: only proven coordinate chains may change.
   Optional output directory emits fixtures for semantic SPIR-V checks. */
#include "nx/gpu/shader/shader.h"
#include "common/utilities/endian.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, cases;
static u64 field(u64 w, int at, int bits, u64 value) {
    u64 mask = ((1ull << bits) - 1) << at;
    return (w & ~mask) | (value << at);
}
static u64 ipa(int axis, int dest) {
    return field(field(0xe003ff870ff7ff00ull, 30, 8, 28 + axis), 0, 8, dest);
}
static u64 f2i(int dest, int source) {
    return field(field(0x5cb0118000070a00ull, 20, 8, source), 0, 8, dest);
}
static u64 mov(int dest, int source) {
    return field(field(0x5c9807800ff70000ull, 20, 8, source), 0, 8, dest);
}
static u64 tld(int a) {
    return 0xdc00000000000000ull | 7ull << 51 | 0xfull << 31 | 2ull << 28 | 0x10ull << 36 |
           0xffull << 20 | 7ull << 16 | (u64)a << 8;
}
static u64 tlds(int encoding, int a, int b) {
    return 0xda00000000000000ull | (u64)encoding << 53 | 0x10ull << 36 | 0xffull << 28 |
           (u64)b << 20 | 7ull << 16 | (u64)a << 8;
}
static u32* translate(const u64* insn, int n, size_t* count) {
    u64 code[128] = {0};
    int at = 10;
    for (int i = 0; i <= n; i++) {
        if ((at - 10) % 4 == 0) code[at++] = 0x001f8000fc0007e0ull;
        code[at++] = i < n ? insn[i] : 0xe30000000007000full;
    }
    for (int i = 0; i < at; i++) code[i] = snx_le64(code[i]);
    ShaderInfo si = {0};
    u32* spv = shader_to_spirv((const u8*)code, at * 8, ST_FS, &si, count, 0, 1, 1, 1);
    if (!spv || si.unsupported) fprintf(stderr, "translation failed: %s\n", si.first_unsupported);
    return spv;
}
static int opcount(const u32* code, size_t n, int op) {
    int count = 0;
    for (size_t i = 5; i < n;) {
        u32 len = code[i] >> 16;
        if (!len || i + len > n) return -9999;
        if ((code[i] & 65535) == (u32)op) count++;
        i += len;
    }
    return count;
}
static void test(const char* dir, const char* name, const u64* insn, int n, int expect) {
    size_t na = 0, nb = 0;
    g_shader_screen_fetch = 0;
    u32* a = translate(insn, n, &na);
    g_shader_screen_fetch = 1;
    u32* b = translate(insn, n, &nb);
    int ok = a && b;
    if (ok && expect)
        ok = opcount(b, nb, 169) == opcount(a, na, 169) + 2 &&
             opcount(b, nb, 110) == opcount(a, na, 110) + 2 &&
             opcount(b, nb, 95) == opcount(a, na, 95);
    else if (ok)
        ok = na == nb && !memcmp(a, b, na * 4);
    cases++;
    if (!ok) { fprintf(stderr, "FAIL %s (expected change=%d)\n", name, expect); failures++; }
    if (dir && ok) for (int enabled = 0; enabled < 2; enabled++) {
        char path[2048];
        snprintf(path, sizeof path, "%s/%s-%d.spv", dir, name, enabled);
        FILE* f = fopen(path, "wb");
        size_t nw = enabled ? nb : na;
        u32* words = enabled ? b : a;
        snx_le32_array(words, nw);
        if (!f || fwrite(words, 4, nw, f) != nw) failures++;
        if (f) fclose(f);
    }
    free(a); free(b);
}

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
        snx_le32_array(spv[enabled], count[enabled]);
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


static u64 twice(int dest, int source) {
    return 0x3868104000070000ull | (u64)source << 8 | dest;
}
static void twice_tests(const char* out) {
    u64 base[] = {ipa(0, 2), ipa(1, 3), twice(4,2), twice(5,3), f2i(2,4), f2i(3,5), tld(2)};
    u64 v[32];
    test(out, "twice-tld", base, 7, 1);
    memcpy(v,base,sizeof base);v[6]=tlds(2,2,3);
    test(out, "twice-tlds", v, 7, 1);
    memcpy(v,base,sizeof base);v[4]=mov(8,4);v[5]=mov(9,5);v[6]=f2i(2,8);v[7]=f2i(3,9);v[8]=tld(2);
    test(out, "twice-float-moves", v, 9, 1);
    memcpy(v,base,sizeof base);v[6]=mov(8,2);v[7]=mov(9,3);v[8]=tld(8);
    test(out, "twice-integer-moves", v, 9, 1);
    memcpy(v,base,sizeof base);v[2]=field(v[2],20,19,0x3fc00);v[3]=field(v[3],20,19,0x3fc00);
    test(out, "twice-reject-1p5", v, 7, 0);
    memcpy(v,base,sizeof base);v[2]=field(v[2],48,1,1);v[3]=field(v[3],48,1,1);
    test(out, "twice-reject-negate", v, 7, 0);
    memcpy(v,base,sizeof base);v[2]=field(v[2],50,1,1);v[3]=field(v[3],50,1,1);
    test(out, "twice-reject-saturate", v, 7, 0);
    memcpy(v,base,sizeof base);v[2]=field(v[2],44,2,2);v[3]=field(v[3],44,2,2);
    test(out, "twice-reject-fmz", v, 7, 0);
    memcpy(v,base,sizeof base);v[2]=field(v[2],41,3,1);v[3]=field(v[3],41,3,1);
    test(out, "twice-reject-postscale", v, 7, 0);
    memcpy(v,base,sizeof base);v[2]=field(v[2],16,4,0);v[3]=field(v[3],16,4,0);
    test(out, "twice-reject-predicated", v, 7, 0);
    memcpy(v,base,sizeof base);v[3]=mov(5,3);
    test(out, "twice-reject-mixed-axes", v, 7, 0);
    memcpy(v,base,sizeof base);v[4]=twice(4,4);v[5]=twice(5,5);v[6]=f2i(2,4);v[7]=f2i(3,5);v[8]=tld(2);
    test(out, "twice-reject-repeat", v, 9, 0);
    memcpy(v,base,sizeof base);v[4]=field(mov(4,255),16,4,0);v[5]=f2i(2,4);v[6]=f2i(3,5);v[7]=tld(2);
    test(out, "twice-reject-ambiguous-write", v, 8, 0);
    memcpy(v,base,sizeof base);v[6]=field(tld(2)|(1ull<<35),20,8,6);
    test(out, "twice-reject-fetch-offset", v, 7, 0);
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : NULL;
    u64 base[] = {ipa(0, 2), ipa(1, 3), f2i(2, 2), f2i(3, 3), tld(2)};
    u64 v[32];
    test(out, "tld", base, 5, 1);
    for (int sign = 0; sign < 2; sign++) for (int round = 0; round < 4; round++) {
        memcpy(v, base, sizeof base);
        v[2] = field(field(v[2], 12, 1, sign), 39, 2, round);
        v[3] = field(field(v[3], 12, 1, sign), 39, 2, round);
        char name[32]; snprintf(name, sizeof name, "round-%d-%d", sign, round);
        test(out, name, v, 5, round == 1 || round == 3);
    }
    memcpy(v, base, sizeof base); v[0] = ipa(1, 2); v[1] = ipa(0, 3);
    test(out, "swapped-axes", v, 5, 0);
    memcpy(v, base, sizeof base); v[0] = mov(2, 255);
    test(out, "arbitrary-coordinate", v, 5, 0);
    memcpy(v, base, sizeof base); v[0] = field(v[0], 51, 1, 1);
    test(out, "saturated-position", v, 5, 0);
    memcpy(v, base, sizeof base); v[0] = field(v[0], 54, 2, 1);
    test(out, "multiplied-position", v, 5, 0);
    memcpy(v, base, sizeof base); v[2] |= 1ull << 49;
    test(out, "absolute-conversion", v, 5, 0);
    memcpy(v, base, sizeof base); v[2] |= 1ull << 45;
    test(out, "negated-conversion", v, 5, 0);
    memcpy(v, base, sizeof base); v[2] = field(v[2], 8, 2, 1);
    test(out, "narrow-conversion", v, 5, 0);
    memcpy(v, base, sizeof base); v[4] = field(v[4] | (1ull << 35), 20, 8, 6);
    test(out, "packed-offset", v, 5, 0);
    memcpy(v, base, sizeof base); v[4] = mov(2, 255); v[5] = base[4];
    test(out, "overwritten-coordinate", v, 6, 0);
    memcpy(v, base, sizeof base); v[4] = field(mov(2, 255), 16, 3, 0); v[5] = base[4];
    test(out, "predicated-overwrite", v, 6, 0);
    memcpy(v, base, sizeof base); v[2] = field(v[2], 16, 3, 0);
    test(out, "predicated-conversion", v, 5, 0);
    memcpy(v, base, sizeof base); v[4] = mov(8, 2); v[5] = mov(9, 3); v[6] = tld(8);
    test(out, "integer-moves", v, 7, 1);
    memcpy(v, base, sizeof base); v[2] = mov(8, 2); v[3] = mov(9, 3);
    v[4] = f2i(2, 8); v[5] = f2i(3, 9); v[6] = tld(2);
    test(out, "float-moves", v, 7, 1);
    memcpy(v, base, sizeof base); v[4] = tlds(2, 2, 3);
    test(out, "tlds-separated", v, 5, 1);
    memcpy(v, base, sizeof base); v[4] = tlds(5, 2, 6);
    test(out, "tlds-lod", v, 5, 1);
    memcpy(v, base, sizeof base); v[4] = tlds(4, 2, 6);
    test(out, "tlds-offset", v, 5, 0);
    memcpy(v, base, sizeof base); v[4] = tlds(8, 6, 2);
    test(out, "tlds-array", v, 5, 1);
    memcpy(v, base, sizeof base); v[4] = field(base[4], 28, 3, 3); v[4] = field(v[4], 8, 8, 1);
    test(out, "tld-array", v, 5, 1);
    memcpy(v, base, sizeof base); v[4] = tlds(6, 2, 6);
    test(out, "tlds-multisample", v, 5, 1);
    memcpy(v, base, sizeof base); v[4] = (0x4c98078805a7001eull & ~0xffull) | 4;
    v[5] = field(0xdd3a0007a0070200ull, 20, 8, 4);
    test(out, "bindless-tld", v, 6, 1);
    /* Unsupported MOV masks do not assign the emitter's destination. */
    for (int mask = 0; mask <= 2; mask += 2) {
        memcpy(v, base, sizeof base);
        v[4] = mov(8, 255); v[5] = mov(9, 255);
        v[6] = field(mov(8, 2), 39, 4, mask);
        v[7] = field(mov(9, 3), 39, 4, mask);
        v[8] = tld(8);
        char name[32]; snprintf(name, sizeof name, "ignored-mov-mask-%d", mask);
        test(out, name, v, 9, 0);
    }
    flow_tests(out);
    twice_tests(out);
    g_shader_screen_fetch = 0;
    printf("screen-fetch: %d cases, %d failures; ShaderInfo=%zu bytes\n", cases, failures, sizeof(ShaderInfo));
    return failures ? 1 : 0;
}
