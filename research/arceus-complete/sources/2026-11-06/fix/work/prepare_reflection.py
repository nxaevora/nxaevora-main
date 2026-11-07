from pathlib import Path
import json, hashlib, shlex, subprocess
root=Path(__file__).resolve().parent.parent
out=root/'work/reflection';out.mkdir(exist_ok=True)
base=Path('C:/Users/brett/Documents/Codex/2026-11-06/recompiled-game-folder-c-users-brett/work/native-shadow-candidate')
build=Path('C:/Users/brett/Documents/Codex/work/nvn-build')
src=Path('C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/gpu/vk')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for e in json.loads((base/'provenance.json').read_text()):
 assert sha(base/e['file'])==e['candidate_sha256']
s=(base/'surface.c').read_text()
start=s.index('static u32 diagnostic_pixel_bytes')
cube=s[start:]
cube=cube.replace('diagnostic_pixel_bytes','reflection_block_bytes').replace('diagnostic_surface_size','reflection_surface_size').replace('vk_surf_diagnostic_capture','vk_surf_reflection_capture')
cube=cube.replace('SNX_MATERIAL_SNAPSHOT','SNX_REFLECTION_CAPTURE').replace('material-snapshot','reflection-snapshot').replace('_attachments.tsv','_reflections.tsv')
cube=cube.replace('case VK_FORMAT_R8_UNORM:', 'case VK_FORMAT_BC6H_SFLOAT_BLOCK:\n        case VK_FORMAT_BC6H_UFLOAT_BLOCK: return 16;\n        case VK_FORMAT_R8_UNORM:')
cube=cube.replace('const Surf* s, u32 present, u32* bpp,','const Surf* s, u32 present, u32 mip, u32* bpp,')
cube=cube.replace('(!s->is_rt && !s->took_drawn)', '(s->type != VK_IMAGE_VIEW_TYPE_CUBE && s->type != VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)')
cube=cube.replace('u64 n = *bpp;', '''u32 w = s->sw >> mip, h = s->sh >> mip;
    if (!w) w = 1; if (!h) h = 1;
    if (w > DIAG_SURF_IMAGE_BYTES || h > DIAG_SURF_IMAGE_BYTES) return 0;
    u32 block = s->f.vk == VK_FORMAT_BC6H_SFLOAT_BLOCK || s->f.vk == VK_FORMAT_BC6H_UFLOAT_BLOCK ? 4 : 1;
    w = (w + block - 1) / block; h = (h + block - 1) / block;
    u64 n = *bpp;''')
cube=cube.replace('s->sw > DIAG_SURF_IMAGE_BYTES / n','w > DIAG_SURF_IMAGE_BYTES / n').replace('n *= s->sw','n *= w').replace('s->sh > DIAG_SURF_IMAGE_BYTES / n','h > DIAG_SURF_IMAGE_BYTES / n').replace('n *= s->sh','n *= h')
cube=cube.replace('u32 bpp;\n        VkImageAspectFlags','u32 bpp, mip;\n        VkImageAspectFlags')
cube=cube.replace('            u32 bpp; u64 size;', '          for (u32 mip = 0; mip < s->levels && mip < 16; mip++) {\n            u32 bpp; u64 size;')
cube=cube.replace('reflection_surface_size(s, present, &bpp','reflection_surface_size(s, present, mip, &bpp')
cube=cube.replace('selected[n].bpp = bpp;', 'selected[n].bpp = bpp; selected[n].mip = mip;')
cube=cube.replace('    int manifest_ok = fprintf', '          }\n    int manifest_ok = fprintf',1)
cube=cube.replace('scope=raw-base-mip-all-array-layers-depth-only-no-stencil','scope=raw-all-cube-mips-all-faces-BC6-blocks-or-uncompressed')
cube=cube.replace('\\toriginal_layout\\twseq\\n','\\toriginal_layout\\twseq\\tblock_width\\tblock_height\\n')
cube=cube.replace('u64 size = selected[i].bytes;', '''u64 size = selected[i].bytes;
        u32 mip = selected[i].mip, w = s->sw >> mip, h = s->sh >> mip;
        if (!w) w = 1; if (!h) h = 1;
        u32 block = s->f.vk == VK_FORMAT_BC6H_SFLOAT_BLOCK || s->f.vk == VK_FORMAT_BC6H_UFLOAT_BLOCK ? 4 : 1;
        u64 row = (u64)((w + block - 1) / block) * selected[i].bpp;
        u64 layer = row * ((h + block - 1) / block);''')
cube=cube.replace('%s_%02u.bin','%s_reflection_%02u.bin').replace('{selected[i].aspect, 0, 0, s->layers}','{selected[i].aspect, mip, 0, s->layers}').replace('.imageExtent = {s->sw, s->sh, 1}', '.imageExtent = {w, h, 1}')
cube=cube.replace('\\t0\\t%llu','\\t%u\\t%llu').replace('\\t%u\\t%llu\\n",','\\t%u\\t%llu\\t%u\\t%u\\n",')
cube=cube.replace('s->w, s->h, s->sw, s->sh,','s->w, s->h, w, h,').replace('s->layers, s->levels, (unsigned long long)size','s->layers, s->levels, mip, (unsigned long long)size')
cube=cube.replace('(unsigned long long)s->sw * selected[i].bpp,\n            (unsigned long long)s->sw * s->sh * selected[i].bpp','(unsigned long long)row,\n            (unsigned long long)layer')
cube=cube.replace('(u32)layout, (unsigned long long)s->wseq)', '(u32)layout, (unsigned long long)s->wseq, block, block)')
# Separate cap and separate readback: ordinary attachment capture stays unchanged.
cube=cube.replace('DIAG_SURF_MAX','256u').replace('DIAG_SURF_TOTAL_BYTES','(64ull << 20)')
s=s[:start]+'static void vk_surf_reflection_capture(const char* prefix);\nvoid vk_reflection_bindings_capture(const char* prefix);\n'+s[start:]
s=s.replace('    const char* enabled = getenv("SNX_MATERIAL_SNAPSHOT");','    vk_reflection_bindings_capture(prefix);\n    vk_surf_reflection_capture(prefix);\n    const char* enabled = getenv("SNX_MATERIAL_SNAPSHOT");',1)
cube=cube.replace('void vk_surf_reflection_capture(', 'static void vk_surf_reflection_capture(')
(out/'surface.c').write_text(s+'\n'+cube)
r=(base/'render.c').read_text()
extra=(root/'work/reflection_bindings.inc').read_text()
anchor='/* Opt-in material metadata for textured geometry.'
r=r.replace(anchor,extra+'\n'+anchor,1)
needle='    material_trace(viewport[0], scissor[0], vs, fs, fres, count, scaled, r);'
assert needle in r
r=r.replace(needle,'    reflection_record(ch, fs, fres);\n'+needle,1)
(out/'render.c').write_text(r)
commands=json.loads((build/'compile_commands.json').read_text())
def run(args,name):
 p=subprocess.run(args,cwd=build,capture_output=True,text=True)
 (out/name).write_text(p.stdout+p.stderr)
 assert p.returncode==0,p.stdout+p.stderr
for name in ['surface.c','render.c']:
 e=next(e for e in commands if Path(e['file'])==src/name)
 args=[x.strip('"') for x in shlex.split(e['command'],posix=False)]
 args[args.index('-o')+1]=str(out/(name+'.obj'));args[-1]=str(out/name)
 run(args+['-Wall','-Wextra'],name+'.compile.txt')
args=json.loads((base/'link-arguments.json').read_text())
args=[str(out/Path(a).name) if a in [str(base/'surface.c.obj'),str(base/'render.c.obj')] else a for a in args]
args[args.index('-o')+1]=str(out/'ArceusReflection-runtime.exe')
run(args,'link.txt')
(out/'link-arguments.json').write_text(json.dumps(args,indent=2))
(out/'provenance.json').write_text(json.dumps({'baseline':sha(base/'ArceusNativeShadow-runtime.exe'),'candidate':sha(out/'ArceusReflection-runtime.exe'),'files':{n:sha(out/n) for n in ['surface.c','render.c']},'game_launched':False},indent=2))
print('Built reflection capture runtime',sha(out/'ArceusReflection-runtime.exe'))
