from pathlib import Path
import json,sys,shlex,struct,subprocess,re,hashlib
p=Path(__file__).resolve().parent;t=p.parent/'taa';root=p.parent.parent;rim=p.parent/'rim-candidate';shared=Path('C:/Users/brett/Documents/Codex/work');build=shared/'nvn-build';srcroot=shared/'nvn-src/src';out=root/'outputs/upscale-runtime'
ccdb=json.loads((build/'compile_commands.json').read_text())
def cc(rel,source,obj):
 e=next(e for e in ccdb if e['file'].replace(chr(92),'/').endswith(rel))
 args=[v.strip('"') for v in shlex.split(e['command'],posix=False)];args[args.index('-o')+1]=str(p/obj);args[-1]=str(p/source);subprocess.run(args+['-I'+str(p)],cwd=build,check=True);return args
def hw(f):return hashlib.sha256(f.read_bytes()).hexdigest()
s=(t/'render.c').read_text();s=s.replace((t/'integration.inc').read_text(),(p/'split.inc').read_text());assert 'vk_fsr3_record(' in s
s=s.replace("static VkDescriptorSet stage_set_(GpuChannel* ch", "static VkImageView up_override;static u64 up_address;\nstatic VkDescriptorSet stage_set_(GpuChannel* ch",1)
s=s.replace('vk_query_draw(cb, ch->id, r[0x574]);','vk_query_draw(cb, ch->id, r[0x574] | (raster_scale>1.1f?0x80000000u:0));')
s=s.replace("    DrawImages images = {0};","    DrawImages images = {0};\n    upscale_before(fs,fres,n?rt[0]:NULL);")
s=s.replace("        u32 b = SNX_VKB_TEX + i;","        if(stage==4 && up_override && t && t->gva==up_address && want==VK_IMAGE_VIEW_TYPE_2D){view=up_override;LOG_LIMIT(12,\"arceus-upscaler: bound reconstructed scene to composite slot %d\\n\",i);layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;if(scaled)*scaled|=1ull<<i;}\n        u32 b = SNX_VKB_TEX + i;")
s=s.replace("zt->sw==tw && zt->sh==thh","zt->sw==1920 && zt->sh==1080").replace("viewport[0].width-tw","viewport[0].width-1920").replace("viewport[0].height)-thh","viewport[0].height)-1080")
a=s.index("    int selected = 0, other = 0;",s.index("static float target_raster_scale"));b=s.index("\n}",a);s=s[:a]+"""    float scale=0;for(u32 i=0;i<=n;i++){Surf *t=i==n?zt:rt[i];if(!t)continue;
        float q=(float)t->sw/t->w;if(scale && fabsf(scale-q)>.001f){LOG_LIMIT(8,"arceus-upscaler: mixed-scale attachments\\n");return 0;}scale=q;}return scale?scale:1;"""+s[b:]
s=s.replace("native_shadow_shape(d->w, d->h, d->fkey) ? 1.0f : g_vk_scale","(float)d->sw/d->w")
cc('/src/nx/gpu/vk/surface.c','surface.c','surface.obj')
(p/'render.c').write_text('#include <math.h>\n'+s)
words=(p/'prepare.spv').read_bytes();(p/'up_words.h').write_text('static const u32 up_words[]={'+','.join(map(hex,struct.unpack('<%dI'%(len(words)//4),words)))+'};')
rargs=cc('/src/nx/gpu/vk/render.c','render.c','render.obj')
qsrc=(srcroot/'nx/gpu/vk/query.c').read_text()
qsrc=qsrc.replace('static u8 g_qown[QN], g_qmul[QN];','static u8 g_qown[QN], g_qmul[QN], up_display[QN];').replace('msaa_samples(msaa_mode);','msaa_samples(msaa_mode&0x7fffffffu);').replace('g_qmul[at] == mul)', 'g_qmul[at] == mul && up_display[at]==(msaa_mode>>31))').replace('g_qmul[at] = (u8)mul;','g_qmul[at] = (u8)mul; up_display[at]=msaa_mode>>31;')
qsrc=qsrc.replace('snx_le64(g_qres[s % QN]) * g_qmul[s % QN]', '(up_display[s % QN] ? (snx_le64(g_qres[s % QN])*g_qmul[s % QN]*9+8)/16 : snx_le64(g_qres[s % QN])*g_qmul[s % QN])').replace('double area = (double)g_vk_scale * (double)g_vk_scale;','double area=1.0;')
(p/'query.c').write_text(qsrc);cc('/src/nx/gpu/vk/query.c','query.c','query.obj')
v='nx/gpu/vk/device.c';s=(srcroot/v).read_text();s=s.replace('    VkDeviceCreateInfo dci =','    if(vk_dlaa_wanted()){SnxVkExtensions e={0}; if(snx_vk_device_extensions(g_vk.phys,&e)){vk_dlaa_device_exts(e.list,e.count,ext,&next,sizeof g_vk.dev_ext/sizeof *ext);snx_vk_extensions_free(&e);}}\n    VkDeviceCreateInfo dci =');s=s.replace('int g_vk_preserve_infnan;','#include "vk_dlaa.h"\nint g_vk_preserve_infnan;');(p/'device.c').write_text(s);cc('/src/'+v,'device.c','device.obj')
v='common/gpu/vk/vk_common.c';s=(srcroot/v).read_text().replace('info->extension_count + 2','info->extension_count + 34').replace('    VkInstanceCreateInfo ci =','    vk_dlaa_instance_exts(ext,&next,info->extension_count+34);\n    VkInstanceCreateInfo ci =');s=s.replace('bool snx_vk_has_instance_extension','#include "vk_dlaa.h"\nbool snx_vk_has_instance_extension');(p/'vk_common.c').write_text(s);cc('/src/'+v,'vk_common.c','common.obj')
headers=next((shared/'nvn-src/.cache/cpm/vulkan-headers').glob('*/include'));volk=next((shared/'nvn-src').rglob('volk.h')).parent
ccbin=shared/'llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe';sdk=root.parent/'files-pasted-by-the-user-project/work/za/third_party/ffx_api'
for f in ['ngx.c','fsr.cpp']:
 subprocess.run([str(ccbin),'-O2','-DVK_NO_PROTOTYPES','-include','volk.h','-I'+str(volk),'-I'+str(headers),'-I'+str(sdk),'-c',str(p/f),'-o',str(p/(f+'.obj'))],check=True)
args=json.loads((rim/'link-arguments.json').read_text());args[args.index(str(p.parent/'reflection/surface.c.obj'))]=str(p/'surface.obj');a=args.index(str(rim/'render.c.obj'));args[a:a+1]=[str(p/f) for f in ['render.obj','query.obj','common.obj','device.obj','ngx.c.obj','fsr.cpp.obj']];exe=out/'ArceusTest.exe';args[args.index('-o')+1]=str(exe);subprocess.run(args,cwd=build,check=True)
for name in ['DLSS','FSR']:
 s=(t/'controller.c').read_text().replace('taa-runtime','upscale-runtime').replace('current-taa-test','current-'+name+'-test').replace('TAA candidate',name+' candidate').replace('Native '+name,name).replace('env(L"SNX_ARCEUS_FPS60"', 'env(L"ARCEUS_UPSCALER",L"'+name+'");env(L"SNX_ARCEUS_FPS60"')
 s=s.replace(r'\"temporal_backend\":\"TAA\"',r'\"temporal_backend\":\"'+name+r'\"')
 s,n=re.subn(r'(static int diagnostic_ok\(void\)\{static const unsigned char h\[32\]=\{)[^}]+',lambda m:m[1]+','.join('0x'+hw(exe)[i:i+2] for i in range(0,64,2)),s);assert n==1
 f=p/(name+'-controller.c');f.write_text(s);launch=root/'outputs'/('Arceus-'+name+'-Test.exe');subprocess.run([str(ccbin),str(f),'-O2','-municode','-mwindows','-DSHADOWMODE=1','-lbcrypt','-o',str(launch)],check=True);subprocess.run([str(launch),'--verify-only'],check=True)
print('Verified launchers for',hw(exe))



