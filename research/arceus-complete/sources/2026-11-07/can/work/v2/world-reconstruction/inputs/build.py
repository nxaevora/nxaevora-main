from pathlib import Path
import hashlib,json,re,shlex,shutil,struct,subprocess,sys
root=Path.cwd(); w=root/'work';tmp=w/'v2'
out=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'outputs'
(out/'world-pt-v2-runtime').mkdir(exist_ok=True,parents=True)
shared=Path.home()/'Documents/Codex/work';fix=shared.parent/'2026-11-06/fix';ups=fix/'work/ups';build=shared/'nvn-build'
compiler=next((shared/'nvn-src/.cache/cpm/glslang-tools').glob('*/bin/glslang.exe'))
defs=[]
for name,flags in [('verifyObjects',['-DVERIFY_OBJECTS']),('mesh',['-DBUILD_MESH']),('calibrate',['-DCALIBRATE']),('contact',[]),('rrContact',['-DRR_GUIDES'])]:
    target=tmp/(name+'.spv')
    subprocess.run([compiler,'-V','--target-env','vulkan1.2',*flags,'-o',target,tmp/'contact.comp'],check=True)
    defs.append('static const u32 '+name+'_words[]={'+','.join(hex(a[0]) for a in struct.iter_unpack('<I',target.read_bytes()))+'};')
target=tmp/'denoise.spv'
subprocess.run([compiler,'-V','--target-env','vulkan1.2','-o',target,tmp/'denoise.comp'],check=True)
defs.append('static const u32 denoise_words[]={'+','.join(hex(a[0]) for a in struct.iter_unpack('<I',target.read_bytes()))+'};')
(tmp/'contact_words.h').write_text('\n'.join(defs))
# The reversed-Z sky has no finite world position. Reproject its direction with
# homogeneous w=0 so translations leave it fixed while camera rotation moves it.
prepare=(ups/'prepare.comp').read_text()
prepare=prepare.replace('uint valid;}','uint valid;float skyDepth;}')
prepare=prepare.replace('z>0 && z<=1','z>=0 && z<=1').replace('vec4(clip,z,1)','vec4(clip,z>0.?z:u.skyDepth,1)')
assert 'z>0.?z:u.skyDepth' in prepare and 'float skyDepth;' in prepare
seed=(ups.parent/'motion/seed.comp').read_text()
seed=seed.replace('    vec2 velocity=vec2(0);','    if(z==0.)return; // Preserve the sky rotation computed by prepare.\n    vec2 velocity=vec2(0);')
for name,source,symbol in [('prepare_sky',prepare,'up_words'),('seed_sky',seed,'sky_seed_words')]:
    (tmp/(name+'.comp')).write_text(source)
    target=tmp/(name+'.spv')
    subprocess.run([compiler,'-V','--target-env','vulkan1.2','-I'+str(ups.parent/'motion'),'-o',target,tmp/(name+'.comp')],check=True)
    (tmp/(name+'.h')).write_text('static const unsigned int '+symbol+'[]={'+','.join(hex(a[0]) for a in struct.iter_unpack('<I',target.read_bytes()))+'};')
flow=(ups.parent/'motion/material_flow.h').read_text().replace('#include "flow_words.h"','#include "../motion/flow_words.h"\n#include "seed_sky.h"').replace('apply_spv,seed_spv','apply_spv,sky_seed_words').replace('sizeof seed_spv','sizeof sky_seed_words')
(tmp/'material_flow_sky.h').write_text(flow)
db=json.loads((build/'compile_commands.json').read_text())
clock=shared/'nvn-src/src/nx/runtime/arceus_clock.c'
source=clock.read_text()
marker='    if (!tracing && !experimental && !message_trace) return;'
assert source.count(marker)==1
source=source.replace(marker,'''    const char *fixed = getenv("SNX_ARCEUS_FIXED_RESOLUTION");
    if (!tracing && !experimental && !message_trace && !(fixed && !strcmp(fixed, "1"))) return;''')
(tmp/'arceus_clock.c').write_text(source)
row=next(e for e in db if e['file'].endswith('/nx/runtime/arceus_clock.c'))
cmd=[v.strip('"') for v in shlex.split(row['command'],posix=False)]
cmd[cmd.index('-o')+1]=str(tmp/'arceus_clock.obj');cmd[-1]=str(tmp/'arceus_clock.c')
subprocess.run(cmd+['-I'+str(clock.parent)],cwd=build,check=True)
e=next(e for e in db if e['file'].endswith('/nx/gpu/vk/render.c'))
args=[v.strip('"') for v in shlex.split(e['command'],posix=False)]
args[args.index('-o')+1]=str(tmp/'render.obj');args[-1]=str(tmp/'render.c')
if '--capture-window' in sys.argv[2:]:
    source=(tmp/'render.c').read_text()
    marker=' static u64 previous_color, previous_color_write, previous_depth_write;'
    assert source.count(marker)==1
    source=source.replace(marker,''' static bool tookMotionSample;
 if(!tookMotionSample && temporal_snapshot.frame==1960 && g_vk_present==1961){
    const char *prefix=getenv("ARCEUS_QA_MOTION");
    if(prefix){
        tookMotionSample=true;
        LOG("arceus-qa: sky depth %.9g at frame %u\\n",-tprev[15]/tprev[11],temporal_snapshot.frame);
        temporal_snapshot_capture(prefix);
    }
 }
'''+marker)
    (tmp/'capture-render.c').write_text(source)
    args[-1]=str(tmp/'capture-render.c')
subprocess.run(args+['-I'+str(ups)],cwd=build,check=True)
# Use the same Vulkan configuration as the renderer for the local NGX binding.
ngx=(ups/'ngx.c').read_text()
start=ngx.index('        // the model:')
end=ngx.index('        // DLAA',start)
ngx=ngx[:start]+'''        const char *quality_presets[] = {
            "DLSS.Hint.Render.Preset.DLAA", "DLSS.Hint.Render.Preset.Quality",
            "DLSS.Hint.Render.Preset.Balanced", "DLSS.Hint.Render.Preset.Performance",
            "DLSS.Hint.Render.Preset.UltraPerformance", "DLSS.Hint.Render.Preset.UltraQuality"
        };
        const unsigned presets[] = {11, 11, 11, 13, 12, 11}; // K, K, K, M, L, K
        for (unsigned i = 0; i < sizeof quality_presets / sizeof *quality_presets; ++i) {
            unsigned requested = 0;
            p_setu(p, quality_presets[i], presets[i]);
            if (!p_getu(p, quality_presets[i], &requested) || requested != presets[i])
                return fail("DLSS model preset parameter verification", 0);
        }
        LOG("[dlaa] NGX preset M for Performance, L for Ultra Performance, K otherwise: verified parameter readback\\n");
'''+ngx[end:]
start=ngx.index("        // DLAA's quality value")
end=ngx.index('        N.fw =',start)
ngx=ngx[:start]+'''        // The 4K build reconstructs twice the scene width and height.
        p_seti(p, "PerfQualityValue", 0); // NVSDK_NGX_PerfQuality_Value_MaxPerf
        NgxResult r = N.create(cmd, NGX_FEATURE_SUPERSAMPLING, p, &N.feat);
        if (!NGX_OK(r) || !N.feat) return fail("DLSS Performance feature creation", r);
        LOG("[dlaa] DLSS Performance mode created\\n");
'''+ngx[end:]
ngx=ngx.replace('void vk_dlaa_init(VkInstance inst', '''typedef struct {
    struct { const wchar_t *const *paths; unsigned count; } search;
    void *internal;
    struct { void (*callback)(const char *,int,int); int level; bool exclusive; } logging;
} NgxCommonInfo;

static void ngx_diagnostic(const char *message, int level, int feature) {
    fprintf(stderr, "[ngx:%d:%d] %s\\n", feature, level, message);
}

void vk_dlaa_init(VkInstance inst''')
ngx=ngx.replace('    NgxResult r = N.init(','''    static const NgxCommonInfo info = {.logging = {ngx_diagnostic, 2, false}};
    const bool diagnostics = getenv("ARCEUS_NGX_LOG") != NULL;
    NgxResult r = N.init(''').replace('NGX_VERSION_API, NULL);','NGX_VERSION_API, diagnostics ? &info : NULL);')
ngx=ngx.replace('#else  // (NGX is Windows-only)', '#include "rr-ngx.inc"\n#else  // (NGX is Windows-only)')
# Fault injection is limited to separately staged regression binaries.
if '--test-rr-create-failure' in sys.argv[2:]:
    rr_source=(tmp/'rr-ngx.inc').read_text()
    call='N.create(cb,13,p,&rr_feature)'
    assert rr_source.count(call)==1
    rr_source=rr_source.replace(call,'(NgxResult)0xbad00001u')
    ngx=ngx.replace('#include "rr-ngx.inc"',rr_source)
(tmp/'ngx.c').write_text(ngx)
ngx_args=args.copy()
ngx_args[ngx_args.index('-o')+1]=str(tmp/'ngx.c.obj')
ngx_args[-1]=str(tmp/'ngx.c')
subprocess.run(ngx_args+['-I'+str(ups),'-include','volk.h'],cwd=build,check=True)
fsr=(ups/'fsr.cpp').read_text()
entry='be.deviceContext = {dev, phys, vkGetDeviceProcAddr};'
assert fsr.count(entry)==1
fsr=fsr.replace('        ffxCreateBackendVKDesc be', '        fsr_output_format = f->out_format == VK_FORMAT_R16G16B16A16_SFLOAT ? ImageFormat::Rgba16f : ImageFormat::Unknown;\n        ffxCreateBackendVKDesc be')
fsr=fsr.replace('static bool load(void)', '#include "fsr-formats.inc"\nstatic bool load(void)').replace(entry,'be.deviceContext = {dev, phys, fsr_device_proc};')
(tmp/'fsr.cpp').write_text(fsr)
cache=shared/'nvn-src/.cache/cpm'
sdk=fix.parent/'files-pasted-by-the-user-project/work/za/third_party/ffx_api'
subprocess.run([shared/'llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe','-O2','-DVK_NO_PROTOTYPES','-include','volk.h',
                '-I'+str(next((cache/'volk').glob('*/volk.h')).parent),'-I'+str(next((cache/'vulkan-headers').glob('*/include'))),
                '-I'+str(compiler.parent.parent/'include'),'-I'+str(sdk),'-I'+str(ups),'-c',tmp/'fsr.cpp','-o',tmp/'fsr.cpp.obj'],check=True)
# Apply the secondary blend-color interface to both new and replayed shader variants.
pipeline=shared/'nvn-src/src/nx/gpu/vk/pipeline.c'
source=pipeline.read_text()
mark='static u32* sh_spirv('
assert source.count(mark)==1
source=source.replace(mark,'#include "dual-source.inc"\n'+mark)
mark='    SNX_ZONE_VALUE(z_translate, *nw);'
assert source.count(mark)==1
source=source.replace(mark,'''    if(spv && stage==ST_FS && mask[6]==1 && !blend_source_module(&spv,nw)){
        free(spv);spv=NULL;
        snprintf(s->si.first_unsupported,sizeof s->si.first_unsupported,"secondary blend color interface");
    }
'''+mark)
(tmp/'pipeline.c').write_text(source)
mark='    p->k.fs = mod_handle(m[4]);'
assert source.count(mark)==1
source=source.replace(mark,mark+'''
    if(p->k.alpha_to_coverage){
        const VkSh *frag=g_pf.sh;
        while(frag && frag->mod!=p->k.fs)frag=frag->next;
        if(!frag || !(frag->si.sph[18]&8))p->k.alpha_to_coverage=0;
    }''')
(tmp/'pipeline.c').write_text(source)
entry=next(e for e in db if e['file'].endswith('/nx/gpu/vk/pipeline.c'))
args=[v.strip('"') for v in shlex.split(entry['command'],posix=False)]
args[args.index('-o')+1]=str(tmp/'pipeline.obj');args[-1]=str(tmp/'pipeline.c')
subprocess.run(args+['-I'+str(pipeline.parent)],cwd=build,check=True)
# Report area changes at real guest reads of a terrain archive header, including revisits.
filesystem=shared/'nvn-src/src/nx/hle/filesystem/fs.c'
src=filesystem.read_text()
mark='    if (!quiet && !__atomic_exchange_n(&rf->logged, 1, __ATOMIC_RELAXED))'
assert src.count(mark)==1
src=src.replace('static int rfile_read(RFile* rf, u64 rel, u8* dst, u64 size, int quiet) {', 'extern u64 arceus_land_request;\nu64 arceus_land_request;\nstatic int rfile_read(RFile* rf, u64 rel, u8* dst, u64 size, int quiet) {')
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
e=next(e for e in db if e['file'].endswith('/nx/gpu/vk/device.c'))
args=[v.strip('"') for v in shlex.split(e['command'],posix=False)]
args[args.index('-o')+1]=str(tmp/'device.obj');args[-1]=str(tmp/'device.c')
subprocess.run(args+['-I'+str(ups)],cwd=build,check=True)
s=(w/'build-merged.py').read_text().split('args=json.loads((fix/',1)[1]
s='args=json.loads((fix/'+s
s=s.replace("str(tmp/'query.obj')","str(w/'query.obj')").replace("str(ups/f) for f","str((tmp if f in ('device.obj','ngx.c.obj','fsr.cpp.obj') else ups)/f) for f").replace('village-v3-4k-runtime','world-pt-v2-runtime').replace('Arceus-Village-v3-DLSS-4K.exe','Arceus-World-PT-v2-4K.exe').replace('current-village-v3-DLSS-4K-test','current-world-pt-v2')
s=s.replace("args[1:1]=[str(village/", "args[1:1]=[str(tmp/'pipeline.obj'),str(tmp/'fs.obj'),str(tmp/'arceus_clock.obj'),str(village/")
if '--capture-window' in sys.argv[2:]:
    # Capture consecutive presents during a pan without making screenshots stall scene loading.
    unit=shared/'nvn-src/src/nx/runtime/platform/unattended.c'
    original=unit.read_text()
    marker='    bucket = frames_now() / g_settings.screenshot_every;'
    assert original.count(marker)==1
    source=original.replace(marker,'''    int presented=frames_now();
    const char *first=getenv("ARCEUS_SHOT_FIRST"),*last=getenv("ARCEUS_SHOT_LAST");
    if(first && presented<atoi(first))return 0;
    if(last && presented>atoi(last))return 0;
    bucket=presented/g_settings.screenshot_every;''')
    source=source.replace('g_settings.screenshot, g_shot_count++','g_settings.screenshot, presented')
    marker='        if (p->done) continue;'
    assert source.count(marker)==1
    source=source.replace(marker,'''        const char *duration=getenv("ARCEUS_PRESS_PRESENTS");
        if(duration && p->at_present){
            unsigned span=(unsigned)atoi(duration);
            if(frames>=0 && (unsigned)frames>=p->at && (unsigned)frames-p->at<span)held|=p->mask;
            continue;
        }
'''+marker)
    (tmp/'unattended.c').write_text('#include <stdlib.h>\n'+source)
    row=next(e for e in db if e['file'].endswith('/platform/unattended.c'))
    cmd=[v.strip('"') for v in shlex.split(row['command'],posix=False)]
    cmd[cmd.index('-o')+1]=str(tmp/'unattended.obj');cmd[-1]=str(tmp/'unattended.c')
    subprocess.run(cmd+['-Wno-unused-variable'],cwd=build,check=True)
    s=s.replace("args[1:1]=[str(tmp/'pipeline.obj')","args[1:1]=[str(tmp/'unattended.obj'),str(tmp/'pipeline.obj')")
s=s.replace("h=hashlib.sha256(exe.read_bytes()).digest()", """s=s.replace('env(L"ARCEUS_UPSCALER"', 'env(L"ARCEUS_TERRAIN_DIR",L"'+str(exe.parent/'terrain').replace(chr(92),chr(92)*2)+'");env(L"ARCEUS_UPSCALER"');h=hashlib.sha256(exe.read_bytes()).digest()""")
exec(s)
for name in ('amd_fidelityfx_loader_vk.dll','amd_fidelityfx_upscaler_vk.dll'):
    shutil.copy2(fix/'outputs/upscale-runtime'/name,exe.parent/name)
