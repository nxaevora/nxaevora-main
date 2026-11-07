from pathlib import Path
import json,shlex,struct,subprocess,hashlib,re,shutil
p=Path(__file__).resolve().parent;root=p.parent.parent;rim=root/'work/rim-candidate';build=Path('C:/Users/brett/Documents/Codex/work/nvn-build')
src=(rim/'render.c').read_text()
def rep(a,b):
 global src
 assert src.count(a)==1,a
 src=src.replace(a,b)
rep('static void reflection_record(', (p/'integration.inc').read_text()+'\nstatic void reflection_record(')
rep('    char path[1100];\n    int n=snprintf(path,sizeof path,"%s_lighting.tsv",prefix);',
    '    temporal_snapshot_capture(prefix);\n    char path[1100];\n    int n=snprintf(path,sizeof path,"%s_lighting.tsv",prefix);')
rep('    resolution_trace(viewport[0]', '''    if(tready && tlast+1==g_vk_present && zt && zt->gva==tzaddr && zt->sw==tw && zt->sh==thh && r[0x4B3] && viewports==1 && fabsf(viewport[0].width-tw)<1 && fabsf(fabsf(viewport[0].height)-thh)<1){
        float j[2];tjitter(j);viewport[0].x+=j[0];viewport[0].y+=j[1];tjitframe=g_vk_present;tjitvalid=1;
    }
    resolution_trace(viewport[0]''')
rep('    g_vk_drawn++;\n}', '    if(n && (writes&1))taa_after(ch,fs,fres,rt[0]);\n    g_vk_drawn++;\n}')
(p/'render.c').write_text(src)
words=(p/'resolve.spv').read_bytes()
(p/'taa_words.h').write_text('static const u32 taa_words[]={' + ','.join(map(hex,struct.unpack('<%dI'%(len(words)//4),words)))+'};\n')
e=next(e for e in json.loads((build/'compile_commands.json').read_text()) if e['file'].replace(chr(92),'/').endswith('/src/nx/gpu/vk/render.c'))
args=[v.strip('"') for v in shlex.split(e['command'],posix=False)];args[args.index('-o')+1]=str(p/'render.c.obj');args[-1]=str(p/'render.c')
subprocess.run(args,cwd=build,check=True)
args=json.loads((rim/'link-arguments.json').read_text());args[args.index(str(rim/'render.c.obj'))]=str(p/'render.c.obj')
rt=root/'outputs/taa-runtime';rt.mkdir(exist_ok=True);exe=rt/'ArceusTest.exe';args[args.index('-o')+1]=str(exe);subprocess.run(args,cwd=build,check=True)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
s=(rim/'controller-rim.c').read_text().replace('rim-runtime','taa-runtime').replace('current-rim-test.txt','current-taa-test.txt').replace('neutral-rim candidate','TAA candidate').replace('Neutral-rim workaround candidate','Native TAA candidate')
s=s.replace('F3 saves cube faces/mips, lighting bindings and attachments','F3 saves temporal motion, adjacent scene features, lighting bindings and attachments')
s=s.replace(r'\"reflection_capture\":true',r'\"temporal_backend\":\"TAA\",\"temporal_capture\":true,\"reflection_capture\":true')
s,n=re.subn(r'(static int diagnostic_ok\(void\)\{static const unsigned char h\[32\]=\{)[^}]+',lambda m:m[1]+','.join('0x'+sha(exe)[i:i+2] for i in range(0,64,2)),s);assert n==1
(p/'controller.c').write_text(s);launcher=root/'outputs/Arceus-TAA-Test.exe'
subprocess.run([e['command'].split(' ')[0].strip('"'),str(p/'controller.c'),'-O2','-Wall','-municode','-mwindows','-DSHADOWMODE=1','-lbcrypt','-o',str(launcher)],check=True)
subprocess.run([str(launcher),'--verify-only'],check=True);print(sha(exe),launcher)
