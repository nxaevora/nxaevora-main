from pathlib import Path
import json,subprocess,shlex,difflib
root=Path(__file__).resolve().parent
work=root.parents[1]; build=work/'nvn-build'; src=work/'nvn-src'
candidate=root/'wide'; candidate.mkdir(exist_ok=True)
relative=Path('nx/gpu/vk/writeback.c'); output=candidate/relative; output.parent.mkdir(parents=True,exist_ok=True)
before=(src/'src'/relative).read_text()
after=(root/'candidate'/relative).read_text().replace('#include <emmintrin.h>','#include <immintrin.h>')
pos=after.index('void vk_write_changed')
after=after[:pos]+'''#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
/* Masked guest accesses touch only bytes changed by the shader. A guest
   write that differs from the original snapshot wins the merge. */
__attribute__((target("avx512f,avx512bw")))
static int merge_mapped_wide(u8* dst, const u8* res, const u8* org, u32 bytes) {
    int changed = 0;
    for (u32 i = 0; i < bytes; i += 64) {
        __m512i original = _mm512_loadu_si512(org + i);
        __m512i result = _mm512_loadu_si512(res + i);
        __mmask64 dirty = _mm512_cmpneq_epi8_mask(result, original);
        if (!dirty) continue;
        __m512i current = _mm512_maskz_loadu_epi8(dirty, dst + i);
        __mmask64 commit = _mm512_mask_cmpeq_epi8_mask(dirty, current, original);
        if (commit) {
            _mm512_mask_storeu_epi8(dst + i, commit, result);
            changed = 1;
        }
    }
    return changed;
}
#endif

'''+after[pos:]
after=after.replace('    for (u32 i = 0; i < size;) {','''    u32 i = 0;
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    if (dst && size >= 64 && __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512f")) {
        i = size & ~63u;
        changed = merge_mapped_wide(dst, res, org, i);
    }
#endif
    for (; i < size;) {''')
output.write_text(after)
test_src=candidate/'test-merge.c'
fixture=(src/'tests/vulkan_release.c').read_text()
fixture=fixture.replace('7, 8, 9, 1023','7, 8, 9, 63, 64, 65, 127, 128, 129, 1023')
fixture=fixture.replace('mapping = 4096;\n\tfor (int direct', 'mapping = 4099;\n\tfor (int direct')
fixture=fixture.replace('vk_write_changed(0x31000, result','vk_write_changed(0x31003, result')
test_src.write_text(fixture)
(candidate/'writeback.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),'a/src/'+relative.as_posix(),'b/src/'+relative.as_posix())))
entry=next(e for e in json.loads((build/'compile_commands.json').read_text()) if Path(e['file']).name=='writeback.c')
args=[a.strip('"') for a in shlex.split(entry['command'],posix=False)]
common=args[:args.index('-o')]
obj=candidate/'writeback.obj'
subprocess.run(common+['-o',str(obj),'-c',str(output)],cwd=build,check=True)
test=candidate/'test-release.exe'
subprocess.run([common[0],'-I'+str(candidate),*common[1:],'-UNDEBUG','-Wno-missing-prototypes','-Wno-missing-variable-declarations',str(test_src),str(src/'src/nx/gpu/vk/pending_pages.c'),'-o',str(test)],cwd=build,check=True)
subprocess.run([str(test)],check=True)
link=json.loads(Path(r'C:\Users\brett\Documents\Codex\2026-11-06\fix\work\rim-candidate\link-arguments.json').read_text())
link.insert(1,str(obj)); link[link.index('-o')+1]=str(candidate/'ArceusTest.exe')
subprocess.run(link,cwd=build,check=True)




