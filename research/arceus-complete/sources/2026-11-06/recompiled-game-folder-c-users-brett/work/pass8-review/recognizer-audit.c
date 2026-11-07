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
    if (dir) for (int enabled = 0; enabled < 2; enabled++) {
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
    /* Audit: MOV mask 0/2 is not emitted as a register assignment by the translator. */
    for (int mask = 0; mask <= 2; mask += 2) {
        memcpy(v, base, sizeof base);
        v[4] = mov(8, 255); v[5] = mov(9, 255);
        v[6] = field(mov(8, 2), 39, 4, mask);
        v[7] = field(mov(9, 3), 39, 4, mask);
        v[8] = tld(8);
        char name[32]; snprintf(name, sizeof name, "ignored-mov-mask-%d", mask);
        test(out, name, v, 9, 0);
    }
    g_shader_screen_fetch = 0;
    printf("screen-fetch: %d cases, %d failures; ShaderInfo=%zu bytes\n", cases, failures, sizeof(ShaderInfo));
    return failures ? 1 : 0;
}
