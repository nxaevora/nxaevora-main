from pathlib import Path
import json,shlex,subprocess,hashlib,re,shutil
here=Path.cwd(); tmp=here/'work'; out=here/'outputs'
base=Path.home()/'Documents/Codex'; fix=base/'2026-11-06/fix'; ups=fix/'work/ups'; shared=base/'work'; build=shared/'nvn-build'
db=json.loads((build/'compile_commands.json').read_text())
for name in ['render','query']:
    s=(ups/(name+'.c')).read_text()
    s=s.replace('target->sw!=2560||target->sh!=1440','target->sw!=3840||target->sh!=2160').replace('*9+8)/16','+2)/4')
    f=tmp/(name+'.c'); f.write_text(s)
    row=next(x for x in db if x['file'].endswith('/nx/gpu/vk/'+name+'.c'))
    args=[v.strip('"') for v in shlex.split(row['command'],posix=False)]
    args[args.index('-o')+1]=str(tmp/(name+'.obj'));args[-1]=str(f)
    subprocess.run(args+['-I'+str(ups)],cwd=build,check=True)
args=json.loads((fix/'work/rim-candidate/link-arguments.json').read_text())
args[args.index(str(fix/'work/reflection/surface.c.obj'))]=str(ups/'surface.obj')
a=args.index(str(fix/'work/rim-candidate/render.c.obj'));args[a:a+1]=[str(tmp/'render.obj'),str(tmp/'query.obj')]+[str(ups/f) for f in ['common.obj','device.obj','ngx.c.obj','fsr.cpp.obj']]
exe=out/'runtime/ArceusTest.exe';args[args.index('-o')+1]=str(exe);subprocess.run(args,cwd=build,check=True)
shutil.copy2(fix/'outputs/upscale-runtime/nvngx_dlss.dll',exe.parent)
s=(ups/'DLSS-controller.c').read_text()
s=re.sub(r'^#define DIAGNOSTIC .*$',lambda _: '#define DIAGNOSTIC L"'+str(exe).replace(chr(92),chr(92)*2)+'"',s,flags=re.M)
s=s.replace('1.3333333333333333','2.0').replace('current-DLSS-test','current-DLSS-4K-test')
h=hashlib.sha256(exe.read_bytes()).digest()
s=re.sub(r'(static int diagnostic_ok\(void\)\{static const unsigned char h\[32\]=\{)[^}]+',lambda m:m[1]+','.join(hex(x) for x in h),s)
f=tmp/'launcher.c';f.write_text(s); launch=out/'Arceus-DLSS-4K.exe'
subprocess.run([str(shared/'llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe'),str(f),'-O2','-municode','-mwindows','-DSHADOWMODE=1','-lbcrypt','-o',str(launch)],check=True)
subprocess.run([str(launch),'--verify-only'],check=True)
