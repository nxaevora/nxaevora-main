#include <windows.h>
#include <stdio.h>
#include "nx/loader/module_abi.h"
int main(void) {
    HMODULE dll = LoadLibraryA("C:\\Users\\brett\\Documents\\Codex\\work\\arceus-games\\ArceusTest\\ArceusTest_code.dll");
    if (!dll) { printf("Load failed: %lu\n", GetLastError()); return 1; }
    const SnxModule* (*describe)(void) = (const SnxModule* (*)(void))GetProcAddress(dll, "snx_module");
    if (!describe) return 2;
    const SnxModule* m = describe();
    printf("ABI=%u, AOT functions=%u, invalidation metadata=%s\n", m->abi, m->nfns,
        m->stale && m->fn_lo && m->fn_len && m->entry_fn ? "present" : "missing");
    const u64 offsets[] = {0x194f110, 0x194f20c, 0x193c04c, 0x27314e8, 0x2482818};
    int found = 0;
    for (unsigned p = 0; p < 5; p++) {
        u64 pc = 0x8005000 + offsets[p];
        for (u32 i = 0; i < m->nfns; i++)
            if (m->fn_lo[i] <= pc && m->fn_lo[i] + m->fn_len[i] >= pc + 16) {
                printf("Patch %llx covered by AOT function %llx, span %u bytes\n",
                    (unsigned long long)offsets[p], (unsigned long long)m->fn_lo[i], m->fn_len[i]);
                found++; break;
            }
    }
    FreeLibrary(dll);
    return found == 5 ? 0 : 3;
}


