from pathlib import Path
import json,shlex,subprocess
root=Path(__file__).resolve().parent
work=root.parents[1]; build=work/'nvn-build'; src=work/'nvn-src'; wide=root/'wide'
source=(wide/'test-merge.c').read_text()
insert=r'''
/* The unmapped half of each vector has no shader writes. Guest reads and
   stores must leave those lanes alone even across a protected page. */
static void check_masked_pages(void) {
    if (!__builtin_cpu_supports("avx512bw") || !__builtin_cpu_supports("avx512f")) return;
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    size_t page = info.dwPageSize;
    u8 *base = VirtualAlloc(NULL, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(base);
    for (unsigned side = 0; side < 2; ++side) {
        u8 *guest = base + page - 23;
        u8 original[64] = {0}, result[64] = {0}, expected[64] = {0};
        memset(guest, 0, 64);
        unsigned first = side ? 0 : 23, end = side ? 23 : 64;
        for (unsigned i = first; i < end; ++i) {
            if (i % 3) result[i] = 47;
            if (i % 5 == 0) guest[i] = 61;
            expected[i] = guest[i] ? guest[i] : result[i];
        }
        DWORD previous;
        assert(VirtualProtect(base + side * page, page, PAGE_NOACCESS, &previous));
        assert(merge_mapped_wide(guest, result, original, 64));
        assert(!merge_mapped_wide(guest, result, original, 64));
        assert(VirtualProtect(base + side * page, page, previous, &previous));
        assert(!memcmp(guest, expected, 64));
    }
    assert(VirtualFree(base, 0, MEM_RELEASE));
    puts("Masked loads and stores passed both protected-page cases.");
}
'''
assert 'int main(void) {' in source
file=wide/'test-page-boundary.c'
file.write_text(source.replace('int main(void) {',insert+'\nint main(void) {\n    check_masked_pages();'))
entry=next(e for e in json.loads((build/'compile_commands.json').read_text()) if Path(e['file']).name=='writeback.c')
args=[a.strip('"') for a in shlex.split(entry['command'],posix=False)]
common=args[:args.index('-o')]
exe=wide/'test-page-boundary.exe'
subprocess.run([common[0],'-I'+str(wide),*common[1:],'-UNDEBUG','-Wno-missing-prototypes','-Wno-missing-variable-declarations',str(file),str(src/'src/nx/gpu/vk/pending_pages.c'),'-o',str(exe)],cwd=build,check=True)
subprocess.run([str(exe)],check=True)

