from pathlib import Path
p=Path('work/v2')
def edit(name,old,new):
 f=p/name;s=f.read_text();assert s.count(old)==1,(name,old,s.count(old));f.write_text(s.replace(old,new))
edit('stream.inc','worldDrawLimit*(sizeof(WorldDrawMaterial)+32),','worldDrawLimit*(sizeof(WorldDrawMaterial)+32)+32,')
edit('contact.h','#include "stream.inc"','#include "stream.inc"\n#include "terrain.inc"')
edit('contact.h','    vkCmdFillBuffer(cb,drawTable.handle','    land_update(cb);\n    vkCmdUpdateBuffer(cb,drawTable.handle,TERRAIN_INFO_OFFSET,sizeof land.gpu,&land.gpu);\n    vkCmdFillBuffer(cb,drawTable.handle')
edit('contact.h','fittedError=%u\\n','fittedError=%u terrainPrimary=%u terrainSecondary=%u terrainShadow=%u\\n')
edit('contact.h','contact.mappedCounters[10]);','contact.mappedCounters[10],contact.mappedCounters[11],contact.mappedCounters[12],contact.mappedCounters[13]);')
edit('build.py','s=(w/\'build-merged.py\')', '''# Report area changes at real guest reads of a terrain archive header, including revisits.
filesystem=shared/'nvn-src/src/nx/hle/filesystem/fs.c'
src=filesystem.read_text()
mark='    if (!quiet && !__atomic_exchange_n(&rf->logged, 1, __ATOMIC_RELAXED))'
assert src.count(mark)==1
src=src.replace('static int rfile_read(RFile* rf, u64 rel, u8* dst, u64 size, int quiet) {', 'extern u64 arceus_land_request;\\nu64 arceus_land_request;\\nstatic int rfile_read(RFile* rf, u64 rel, u8* dst, u64 size, int quiet) {')
src=src.replace(mark,"""    if(!quiet && !rel && size && node->parent && !strcmp(node->parent->name,"terrain") &&
        !strncmp(node->name,"ha_area",7) && node->name[7]>='0' && node->name[7]<='9' &&
        node->name[8]>='0' && node->name[8]<='9'){
        u32 area=(node->name[7]-'0')*10+node->name[8]-'0';
        u64 request=!strcmp(node->name+9,".gfpak") && area<12 ? ((u64)node->size<<8)|(area+1) : 0;
        __atomic_store_n(&arceus_land_request,request,__ATOMIC_RELEASE);
    }
"""+mark)
(tmp/'fs.c').write_text(src)
entry=next(e for e in db if e['file'].endswith('/nx/hle/filesystem/fs.c'))
args=[v.strip('"') for v in shlex.split(entry['command'],posix=False)]
args[args.index('-o')+1]=str(tmp/'fs.obj');args[-1]=str(tmp/'fs.c')
subprocess.run(args+['-I'+str(filesystem.parent)],cwd=build,check=True)
s=(w/'build-merged.py')''')
edit('build.py','exec(s)', '''s=s.replace("args[1:1]=[str(village/", "args[1:1]=[str(tmp/'fs.obj'),str(village/")
s=s.replace("h=hashlib.sha256(exe.read_bytes()).digest()", """s=s.replace('env(L"ARCEUS_UPSCALER"', 'env(L"ARCEUS_TERRAIN_DIR",L"'+str(exe.parent/'terrain').replace(chr(92),chr(92)*2)+'");env(L"ARCEUS_UPSCALER"');h=hashlib.sha256(exe.read_bytes()).digest()""")
exec(s)''')
