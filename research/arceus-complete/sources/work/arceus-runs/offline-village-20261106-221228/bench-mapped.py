from pathlib import Path
import json,shlex,subprocess,sys,statistics
base=Path(__file__).resolve().parent
out=base/'mapped';work=base.parents[1];build=work/'nvn-build';src=work/'nvn-src'
db=json.loads((build/'compile_commands.json').read_text())
orig=src/'src/nx/memory/mem.c'
entry=next(e for e in db if Path(e['file'])==orig)
argv=[s.strip('"') for s in shlex.split(entry['command'],posix=False)]
flags=argv[:argv.index('-o')]+['-I'+str(orig.parent)]
def compile(p,obj):
    subprocess.run(flags+['-c',str(p),'-o',str(obj)],cwd=build,check=True)
link=json.loads(Path(r'C:\Users\brett\Documents\Codex\2026-11-06\fix\work\rim-candidate\link-arguments.json').read_text())
link.remove('src/multicall/CMakeFiles/scarletnx.dir/main.c.obj');link[link.index('-mwindows')]='-mconsole'
loop=out/'lookup-loop.c'
loop.write_text(r'''#include "nx/memory/mem.h"
u64 run_lookups(unsigned count);
u64 run_lookups(unsigned count) {
    u64 sum = 0;
    for (unsigned n=0;n<count;n++) {
        u64 address = 0x1000 + ((n * 4099u) & 0x3ffffffu);
        sum += mem_mapped_run(address, (n & 7u) ? 1 : 4096);
    }
    return sum;
}
''')
compile(loop,out/'lookup-loop.obj')
for flavor,path in [('v2',orig),('v3',out/'mem.c')]:
    p=out/('bench-'+flavor+'.c');obj=p.with_suffix('.obj');exe=p.with_suffix('.exe')
    p.write_text('#include "'+path.as_posix()+'"\n'+r'''
extern u64 run_lookups(unsigned count);
int main(void) {
    static MemBlock block;
    block.size=AS_END;block.state=MS_NORMAL;g_blk=&block;g_nblk=1;
    const unsigned count=32000000;
    LARGE_INTEGER freq, start, stop;
    QueryPerformanceFrequency(&freq);
    (void)run_lookups(1000000);
    QueryPerformanceCounter(&start);
    u64 sum=run_lookups(count);
    QueryPerformanceCounter(&stop);
    if(sum!=(u64)(count/8)*(4096+7)) return 1;
    printf("%.5f\n",1e9*(stop.QuadPart-start.QuadPart)/freq.QuadPart/count);
    return 0;
}
''')
    compile(p,obj)
    args=link.copy();args[1:1]=[str(obj),str(out/'lookup-loop.obj')];args[args.index('-o')+1]=str(exe)
    subprocess.run(args,cwd=build,check=True)
rows=[]
for flavor in ['v2','v3','v3','v2']*3:
    val=float(subprocess.check_output([str(out/('bench-'+flavor+'.exe'))],cwd=build,text=True))
    rows.append((flavor,val))
(out/'lookup-microbench.csv').write_text('build,ns_per_cached_call\n'+''.join('%s,%.5f\n'%row for row in rows))
for name in ['v2','v3']:print(name,statistics.median(value for f,value in rows if f==name),'ns / cached call')

