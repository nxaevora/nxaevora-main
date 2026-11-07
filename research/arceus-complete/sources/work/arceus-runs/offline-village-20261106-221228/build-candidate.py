from pathlib import Path
import json,subprocess,shlex,sys,difflib
root=Path(__file__).resolve().parent
work=root.parents[1]; build=work/'nvn-build'; src=work/'nvn-src'; original=src/'src/nx/gpu/vk/writeback.c'
candidate=root/'candidate'; candidate.mkdir(exist_ok=True)
relative=Path('nx/gpu/vk/writeback.c'); output=candidate/relative
output.parent.mkdir(parents=True,exist_ok=True)
before=original.read_text()
anchor='        do {\n            if (end - i >= 8'
assert before.count(anchor)==1
after=before.replace('#include <stdlib.h>', '''#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#include <stdlib.h>''').replace(anchor,'''#if defined(__SSE2__)
        /* The mapped path can commit individual bytes directly. Compare the two
           host snapshots first so an unchanged shader lane never touches guest memory. */
        if (dst) {
            while (end - i >= 16) {
                __m128i old = _mm_loadu_si128((const __m128i *)(org + i));
                __m128i result = _mm_loadu_si128((const __m128i *)(res + i));
                unsigned pending = 0xffffu ^ (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(old, result));
                if (pending == 0xffffu && !memcmp(dst + i, org + i, 16)) {
                    memcpy(dst + i, res + i, 16);
                    changed = 1;
                } else {
                    while (pending) {
                        unsigned lane = (unsigned)__builtin_ctz(pending);
                        if (dst[i + lane] == org[i + lane]) {
                            dst[i + lane] = res[i + lane];
                            changed = 1;
                        }
                        pending &= pending - 1;
                    }
                }
                i += 16;
            }
            if (i == end) continue;
        }
#endif
        do {
            if (end - i >= 8''')
output.write_text(after)
(candidate/'writeback.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),'a/src/'+relative.as_posix(),'b/src/'+relative.as_posix())))
entry=next(e for e in json.loads((build/'compile_commands.json').read_text()) if Path(e['file']).name=='writeback.c')
args=[a.strip('"') for a in shlex.split(entry['command'],posix=False)]
common=args[:args.index('-o')]
obj=candidate/'writeback.obj'
subprocess.run(common+['-o',str(obj),'-c',str(output)],cwd=build,check=True)
test=candidate/'test-release.exe'
subprocess.run([common[0],'-I'+str(candidate),*common[1:],'-UNDEBUG','-Wno-missing-prototypes','-Wno-missing-variable-declarations',str(src/'tests/vulkan_release.c'),str(src/'src/nx/gpu/vk/pending_pages.c'),'-o',str(test)],cwd=build,check=True)
subprocess.run([str(test)],check=True)
link=json.loads(Path(r'C:\Users\brett\Documents\Codex\2026-11-06\fix\work\rim-candidate\link-arguments.json').read_text())
link.insert(1,str(obj)); link[link.index('-o')+1]=str(candidate/'ArceusTest.exe')
subprocess.run(link,cwd=build,check=True)


