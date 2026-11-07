from pathlib import Path
import subprocess,shlex,hashlib,json
root=Path(__file__).resolve().parent.parent
out=root/'work/rim-candidate';out.mkdir(exist_ok=True)
base=root/'work/reflection';build=Path('C:/Users/brett/Documents/Codex/work/nvn-build')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
proof=json.loads((base/'provenance.json').read_text())
assert sha(base/'render.c')==proof['files']['render.c']
s=(base/'render.c').read_text();extra=(root/'work/rim_candidate.inc').read_text()
anchor='static VkDescriptorSet stage_set_('
s=s.replace(anchor,extra+'\n'+anchor,1)
needle='        if (!cb_get(ch, stage, i, sz, &b, &o)) return VK_NULL_HANDLE;'
assert s.count(needle)==1
s=s.replace(needle,needle+'\n        rim_candidate_buffer(ch,stage,i,s,sz,&b,&o);')
# Captures explicitly distinguish original CPU constants from candidate-effective copies.
needle='        memcpy(d->cb[i].data,p,n); d->cb[i].status = 1;'
assert needle in s
s=s.replace(needle,needle+'''\n        if (rim_candidate_enabled() && rim_candidate_match(d->cb[i].data,n,4,i,fs->hash))
            rim_candidate_apply(d->cb[i].data);''')
s=s.replace('constants=CPU-at-draw-not-GPU-readback','constants=CPU-at-draw-with-enabled-rim-transform-not-GPU-readback')
(out/'render.c').write_text(s)
cc=json.loads((build/'compile_commands.json').read_text())
e=next(e for e in cc if e['file'].replace('\\','/').endswith('/src/nx/gpu/vk/render.c'))
args=[v.strip('"') for v in shlex.split(e['command'],posix=False)];args[args.index('-o')+1]=str(out/'render.c.obj');args[-1]=str(out/'render.c')
def run(args,name):
 p=subprocess.run(args,cwd=build,capture_output=True,text=True);(out/name).write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
run(args+['-Wall','-Wextra'],'compile.txt')
args=json.loads((base/'link-arguments.json').read_text());args=[str(out/'render.c.obj') if a==str(base/'render.c.obj') else a for a in args];args[args.index('-o')+1]=str(out/'ArceusRimCandidate-runtime.exe')
run(args,'link.txt');(out/'link-arguments.json').write_text(json.dumps(args,indent=2))
(out/'provenance.json').write_text(json.dumps({'diagnostic_base':proof,'candidate_sha256':sha(out/'ArceusRimCandidate-runtime.exe'),'render_sha256':sha(out/'render.c'),'game_launched':False},indent=2))
print('Built isolated neutral-rim workaround candidate',sha(out/'ArceusRimCandidate-runtime.exe'))
