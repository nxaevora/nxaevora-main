import difflib,json,shlex,subprocess
from pathlib import Path
base=Path(__file__).resolve().parent
work=base.parents[1]; build=work/'nvn-build'; src=work/'nvn-src'
out=base/'mapped'; out.mkdir(exist_ok=True)
original=src/'src/nx/memory/mem.c'; before=original.read_text()
def once(s,a,b):
    assert s.count(a)==1,a
    return s.replace(a,b)
after=once(before,'u64 lo[MEM_RUNS], hi[MEM_RUNS], gen;', 'struct { u64 lo, hi; } slot[MEM_RUNS];\n    u64 gen;')
start=after.index('u64 mem_mapped_run(u64 addr'); hit=after.index('    if (mine) mem_lock();',start)
after=after[:start]+'''/* Keep the lock and block walk out of the cached lookup's register frame. */
static __attribute__((noinline)) u64 mapped_run_miss(u64 addr, u64 size, u32 i) {
    u64 end, lo, lim = addr, run, gen;
    int mine = !t_mem_held;
'''+after[hit:]
after=after.replace('t_runs.lo[i]', 't_runs.slot[i].lo').replace('t_runs.hi[i]', 't_runs.slot[i].hi')
after=once(after,'/* the state and permission of mapped memory', '''u64 mem_mapped_run(u64 addr, u64 size) {
    if (!size) return 0;
    u32 i = MEM_RUN_SLOT(addr);
    if (__atomic_load_n(&g_unmap_gen, __ATOMIC_ACQUIRE) == t_runs.gen &&
        addr >= t_runs.slot[i].lo && addr < t_runs.slot[i].hi &&
        size <= t_runs.slot[i].hi - addr)
        return size;
    return mapped_run_miss(addr, size, i);
}
/* the state and permission of mapped memory''')
(out/'mem.c').write_text(after)
(out/'mem-mapped-run.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),'a/src/nx/memory/mem.c','b/src/nx/memory/mem.c')))
db=json.loads((build/'compile_commands.json').read_text())
cmd=next(e['command'] for e in db if Path(e['file'])==original)
args=[a.strip('"') for a in shlex.split(cmd,posix=False)]
flags=args[:args.index('-o')]+['-I'+str(original.parent)]
link=json.loads(Path(r'C:\Users\brett\Documents\Codex\2026-11-06\fix\work\rim-candidate\link-arguments.json').read_text())
link.insert(1,str(base/'wide/writeback.obj'))
def make(source,obj,exe,test=False):
    subprocess.run(flags+['-UNDEBUG','-c',str(source),'-o',str(obj)],cwd=build,check=True)
    call=link.copy()
    if test:
        call.remove('src/multicall/CMakeFiles/scarletnx.dir/main.c.obj')
        call[call.index('-mwindows')]='-mconsole'
    call.insert(1,str(obj));call[call.index('-o')+1]=str(exe)
    subprocess.run(call,cwd=build,check=True)
test=out/'lookup-test.c'
test.write_text(r'''#include "mem.c"
#include <assert.h>
int main(void) {
    static MemBlock blocks[260];
    assert(mem_mapped_run(123, 9) == 9 && mem_mapped_run(123, 0) == 0);
    const u64 unit = 1u << 20;
    g_blk = blocks; g_nblk = 260;
    for (unsigned j=0; j<260; j++) {
        blocks[j].addr = j*unit;
        blocks[j].size = unit;
        blocks[j].state = MS_NORMAL;
    }
    blocks[259].size = AS_END-blocks[259].addr;
    for (unsigned pass=0; pass<12; pass++) {
        mem_lock(); runs_stale_nl();
        for (unsigned j=0;j<259;j++)
            blocks[j].state=(j+pass)%7==0?MS_FREE:(j+pass)%13==0?MS_INACCESSIBLE:MS_NORMAL;
        mem_unlock();
        for(unsigned j=0;j<259;j++) {
            unsigned k=j;
            while(k<260 && blocks[k].state!=MS_FREE && blocks[k].state!=MS_INACCESSIBLE) k++;
            u64 addr=(j+1)*unit-3;
            u64 available=k==j?0:(k==260?AS_END:k*unit)-addr;
            const u64 sizes[]={0,1,3,4,unit,UINT64_MAX};
            for(unsigned n=0;n<sizeof sizes/sizeof *sizes;n++) {
                u64 expect=sizes[n]<available?sizes[n]:available;
                assert(mem_mapped_run(addr,sizes[n])==expect);
            }
        }
        mem_lock();
        assert(mem_mapped_run(AS_END-1,8)==1 && mem_mapped_run(AS_END,1)==0);
        mem_unlock();
    }
    puts("Mapped-run boundaries, cache collisions, held lock, holes and invalidations passed.");
    return 0;
}
''')
make(test,out/'lookup-test.obj',out/'lookup-test.exe',True)
subprocess.run([str(out/'lookup-test.exe')],cwd=build,check=True)
make(out/'mem.c',out/'mem.obj',out/'ArceusTest.exe')

