import sys
sys.path.pop(0)
from pathlib import Path
import json,hashlib,difflib,subprocess,shlex
root=Path.cwd(); out=root/'work/native-shadow-candidate';out.mkdir(exist_ok=True)
src=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/gpu/vk')
build=Path(r'C:/Users/brett/Documents/Codex/work/nvn-build')
common='''
/* Explicit Arceus experiment: the observed 1024-square D32/tag shadow pair.
   Disabled by default; no scene color/depth format is selected. */
static int native_shadow_shape(u32 w, u32 h, u32 fkey) {
    const char* flag = getenv("SNX_ARCEUS_NATIVE_SHADOWS");
    return flag && !strcmp(flag, "1") && w == 1024 && h == 1024 &&
           (fkey == 0x1000a || fkey == 0xf6);
}
'''
def scale_calls(s):
    while 'vk_scf(' in s:
        a=s.index('vk_scf(');i=a+7;start=i;depth=1
        while depth:
            if s[i]=='(':depth+=1
            if s[i]==')':depth-=1
            i+=1
        s=s[:a]+'((float)('+s[start:i-1]+') * raster_scale)'+s[i:]
    return s
changes={}
s=(src/'surface.c').read_text();old=s
s=s.replace('static Surf* surf_create(',common+'\nstatic Surf* surf_create(',1)
s=s.replace('s->sh = is_rt ? vk_sc(h) : h;','''s->sh = is_rt ? vk_sc(h) : h;
    if (is_rt && native_shadow_shape(w, h, fkey)) {
        s->sw = w; s->sh = h;
        LOG_LIMIT(8, "arceus-native-shadow: target=%llx format=%x native=%ux%u\\n",
                  (unsigned long long)gva, fkey, w, h);
    }''',1)
changes['surface.c']=(old,s)
s=(src/'render.c').read_text();old=s
anchor='/* v: bit 0 depth, 1 stencil'
pos=s.index(anchor)
s=s[:pos]+common+'''
static float target_raster_scale(Surf** rt, Surf* zt, u32 n) {
    int selected = 0, other = 0;
    for (u32 i = 0; i <= n; i++) {
        const Surf* t = i == n ? zt : rt[i];
        if (!t) continue;
        if (!native_shadow_shape(t->w, t->h, t->fkey) || t->sw != t->w || t->sh != t->h)
            other = 1;
        else selected = 1;
    }
    if (selected && other && g_vk_scale != 1.0f) {
        LOG_LIMIT(8, "arceus-native-shadow: mixed attachment scales; experimental pass skipped\\n");
        return 0.0f;
    }
    return selected ? 1.0f : g_vk_scale;
}
''' + s[pos:]
# Clear and draw select from actual attachments, never mutate global scale.
needle='read_targets(ch, rt, &zt, &n);'
assert s.count(needle)==3
# Clear: all bound attachments. Draw: select only after unused targets are removed.
s=s.replace(needle,needle+'\n    float raster_scale = target_raster_scale(rt, zt, n);\n    if (!raster_scale) return;',1)
anchor='    /* nothing drawn to, captured or stored.'
assert s.count(anchor)==1
s=s.replace(anchor,'    float raster_scale = target_raster_scale(rt, zt, n);\n    if (!raster_scale) { g_vk_skipped++; return; }\n'+anchor,1)
a=s.index('void vk_render_clear(');b=s.index('static int frame_consts(',a)
s=s[:a]+scale_calls(s[a:b])+s[b:]
s=s.replace('static int frame_consts(VkBuffer* buf, u64* off)', 'static int frame_consts(VkBuffer* buf, u64* off, float raster_scale)')
s=s.replace('static u64 frame = ~0ull, o;', 'static u64 frame = ~0ull, o;\n    static float cached_scale;')
s=s.replace('if (frame != vk_submit_number()) {','if (frame != vk_submit_number() || cached_scale != raster_scale) {',1)
s=s.replace('float rs[4] = {1 / g_vk_scale, 1 / g_vk_scale, g_vk_scale, 0}', 'float rs[4] = {1 / raster_scale, 1 / raster_scale, raster_scale, 0}')
a=s.index('static int frame_consts(');b=s.index('\ntypedef struct {',a)
s=s[:a]+s[a:b].replace('frame = vk_submit_number();','frame = vk_submit_number();\n        cached_scale = raster_scale;',1)+s[b:]
# stage_set gets the draw-specific constant explicitly.
a=s.index('static VkDescriptorSet stage_set_(');b=s.index('    u32* r = ch->r3d;',a)
part=s[a:b]
assert part.count('DrawImages* images)')==3
s=s[:a]+part.replace('DrawImages* images)', 'DrawImages* images, float raster_scale)')+s[b:]
s=s.replace('fb, scaled, res, draw, images);','fb, scaled, res, draw, images, raster_scale);',1)
s=s.replace('frame_consts(&cb, &co)', 'frame_consts(&cb, &co, raster_scale)')
s=s.replace('frame_consts(&vbuf[v], &voff[v])','frame_consts(&vbuf[v], &voff[v], raster_scale)')
for end in ['NULL, &storage, &images);','fres, &storage, &images) :','NULL, &storage, NULL);']:
    assert end in s,end
    s=s.replace(end,end.replace(')',', raster_scale)'),1)
a=s.index('VkViewport vk_render_viewport_at(');b=s.index('VkViewport vk_render_viewport(const',a)
part=s[a:b].replace('VkViewport vk_render_viewport_at(const u32* r, u32 index)',
    'static VkViewport render_viewport_scaled(const u32* r, u32 index, float raster_scale)')
part=scale_calls(part)+'''VkViewport vk_render_viewport_at(const u32* r, u32 index) {
    return render_viewport_scaled(r, index, g_vk_scale);
}
'''
s=s[:a]+part+s[b:]
a=s.index('static void render_draw(');b=s.index('void vk_render_draw_texture(',a)
part=scale_calls(s[a:b]).replace('vk_render_viewport(r)','render_viewport_scaled(r, 0, raster_scale)').replace('vk_render_viewport_at(r, i)','render_viewport_scaled(r, i, raster_scale)')
s=s[:a]+part+s[b:]
# Blit destination uses its own allocation ratio (also covers native tag maps).
a=s.index('void vk_render_draw_texture(')
part=s[a:].replace('float tsx =', 'float dsx = native_shadow_shape(d->w, d->h, d->fkey) ? 1.0f : g_vk_scale;\n    float dsy = dsx;\n    float tsx =',1)
for arg,axis in [('x0','dsx'),('y0','dsy'),('x0 + w','dsx'),('y0 + h','dsy')]:
    part=part.replace('vk_scf('+arg+')','(('+arg+') * '+axis+')')
s=s[:a]+part
changes['render.c']=(old,s)
s=(src/'texture.c').read_text();old=s
needle='c->sw = vk_sc(w) < rt->sw ? vk_sc(w) : rt->sw;'
assert s.count(needle)==1
s=s.replace(needle,'u32 cw = rt->sw == rt->w ? w : vk_sc(w);\n        u32 ch = rt->sh == rt->h ? h : vk_sc(h);\n        c->sw = cw < rt->sw ? cw : rt->sw;')
needle='c->sh = vk_sc(h) < rt->sh ? vk_sc(h) : rt->sh;'
assert s.count(needle)==1
s=s.replace(needle,'c->sh = ch < rt->sh ? ch : rt->sh;')
changes['texture.c']=(old,s)
manifest=[]
commands=json.loads((build/'compile_commands.json').read_text())
for name,(before,after) in changes.items():
    (out/(name+'.before')).write_bytes(before.encode());(out/name).write_bytes(after.encode())
    (out/(name+'.patch')).write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile=name,tofile=name)))
    entry=next(e for e in commands if Path(e['file'])==src/name)
    args=shlex.split(entry['command'],posix=False);args=[x.strip('"') for x in args]
    args[args.index('-o')+1]=str(out/(name+'.obj'));args[-1]=str(out/name)
    args+=['-Wall','-Wextra']
    result=subprocess.run(args,cwd=build,capture_output=True,text=True)
    (out/(name+'.compile.txt')).write_text(result.stdout+result.stderr)
    assert result.returncode==0,result.stderr
    assert (src/name).read_text()==before,'Shared source changed during compilation'
    manifest.append(dict(file=name,base_sha256=hashlib.sha256((src/name).read_bytes()).hexdigest(),candidate_sha256=hashlib.sha256((out/name).read_bytes()).hexdigest()))
(out/'provenance.json').write_text(json.dumps(manifest,indent=2))
print('Private candidate objects compiled; shared source unchanged.')
