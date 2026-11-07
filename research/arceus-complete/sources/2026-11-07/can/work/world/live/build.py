from pathlib import Path
import subprocess,struct,json,shlex,shutil,re,hashlib
root=Path.cwd();w=root/'work';tmp=w/'world/live';out=root/'outputs';shared=Path.home()/'Documents/Codex/work'
fix=shared.parent/'2026-11-06/fix';ups=fix/'work/ups';build=shared/'nvn-build'
compiler=next((shared/'nvn-src/.cache/cpm/glslang-tools').glob('*/bin/glslang.exe'))
defs=[]
for name,flags in [('mesh',['-DBUILD_MESH']),('contact',[])]:
    target=tmp/(name+'.spv')
    subprocess.run([compiler,'-V','--target-env','vulkan1.2',*flags,'-o',target,tmp/'contact.comp'],check=True)
    defs.append('static const u32 '+name+'_words[]={'+','.join(hex(a[0]) for a in struct.iter_unpack('<I',target.read_bytes()))+'};')
(tmp/'contact_words.h').write_text('\n'.join(defs))
host=(w/'pt/contact.h').read_text().replace('240*135*2','500000')
host=host.replace('static void contact_record(', '#include "stream.inc"\nstatic void contact_record(')
host=host.replace('if(!arceus_contact_supported)return;', 'if(!world.used || !world.clip.address || world.frame!=g_vk_present)return;')
host=host.replace('u32 frame;} pc','u32 frame,count;u64 clip;u32 padding[2];float sun[4];} pc')
host=host.replace('.frame=g_vk_present};','.frame=g_vk_present,.count=world.used/3,.clip=world.clip.address};')
host=host.replace('    if(contact.mappedCounters &&', '''    float horizontal[3],vertical[3],forward[3];
    for(int i=0;i<3;i++){horizontal[i]=camera[i]/pc.camera[0];vertical[i]=-camera[i+4]/pc.camera[1];}
    for(int i=0;i<3;i++)forward[i]=horizontal[(i+1)%3]*vertical[(i+2)%3]-horizontal[(i+2)%3]*vertical[(i+1)%3];
    const float sun[3]={.37039f,.889f,.269f};
    for(int i=0;i<3;i++){pc.sun[0]+=horizontal[i]*sun[i];pc.sun[1]+=vertical[i]*sun[i];pc.sun[2]+=forward[i]*sun[i];}
    contact.bottom.range.primitiveCount=pc.count;
    LOG_LIMIT(6,"world-pt: submitted %u triangles, %u over-cap draws\\n",pc.count,world.skipped);
    if(contact.mappedCounters &&''')
host=host.replace('vkCmdDispatch(cb,30,17,1);','vkCmdDispatch(cb,(pc.count+63)/64,1,1);').replace('sampled AO rays=%u hits=%u secondBounceHits=%u litSecondBounces=%u','sampled primary=%u primaryHits=%u matchingDepth=%u secondaryHits=%u')
host=host.replace('float sun[4];','float axes[3][4];')
a=host.index('    const float sun[')
b=host.index('    contact.bottom.range',a)
host=host[:a]+"""    float len=0;
    for(int i=0;i<3;i++){
        forward[i]=camera[12+i]-(camera[8+i]/camera[11])*camera[15];len+=forward[i]*forward[i];
    }
    if(!(len>1.e-10f) || !isfinite(len))return;
    for(int i=0;i<3;i++){pc.axes[0][i]=horizontal[i];pc.axes[1][i]=vertical[i];pc.axes[2][i]=forward[i]/sqrtf(len);}
"""+host[b:]
host=host.replace('if(!world.used ||','if(world.used<300 ||')
(tmp/'contact.h').write_text(host)
s=(w/'world/render.c').read_text().replace('#include "capture.h"','').replace('vk_frame_check();\n    if (g_draw.check','vk_frame_check();\n    world_frame();\n    if (g_draw.check')
s=s.replace('n>=2 && captureCount','n>=2 && (writes&2) && rt[1] && rt[1]->f.vk==VK_FORMAT_R8G8B8A8_SRGB && rt[1]->gva==0x552a80000ull && captureCount')
s=s.replace('world_capture_begin(cb,(u32)captureCount)','world_capture_begin(cb,(u32)captureCount,rt[1])')
s=s.replace('VkDescriptorSetLayoutBinding b[5];for(int i=0;i<5;i++)','VkDescriptorSetLayoutBinding b[6];for(int i=0;i<6;i++)').replace('(i==0||i==2)?VK_DESCRIPTOR_TYPE','(i==0||i==2||i==5)?VK_DESCRIPTOR_TYPE').replace('.bindingCount=5,.pBindings=b','.bindingCount=6,.pBindings=b').replace('VK_SHADER_STAGE_COMPUTE_BIT,0,76','VK_SHADER_STAGE_COMPUTE_BIT,0,80')
s=s.replace(' VkImageView views[]={zv,motion->view,cv,color->view,depth->view};VkDescriptorImageInfo ii[5];VkWriteDescriptorSet wr[5];',''' VkImageView albedoView=VK_NULL_HANDLE;
 if(world.albedoAddress){
     for(Surf *surface=vk_surf_first(world.albedoAddress);surface;surface=surface->next){
         if(surface->gva!=world.albedoAddress||surface->f.vk!=VK_FORMAT_R8G8B8A8_SRGB||surface->layout==VK_IMAGE_LAYOUT_UNDEFINED||surface->sw!=w||surface->sh!=h)continue;
         albedoView=vk_tex_view(surface,VK_IMAGE_VIEW_TYPE_2D,0,&id,0);
         if(albedoView){vk_surf_touch(surface);vk_surf_use(cb,surface,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);break;}
     }
 }
 VkImageView views[]={zv,motion->view,cv,color->view,depth->view,albedoView?albedoView:cv};VkDescriptorImageInfo ii[6];VkWriteDescriptorSet wr[6];''').replace('for(int i=0;i<5;i++){int sampled=i==0||i==2;', 'for(int i=0;i<6;i++){int sampled=i==0||i==2||i==5;').replace('vkUpdateDescriptorSets(g_vk.dev,5,wr','vkUpdateDescriptorSets(g_vk.dev,6,wr').replace(' contact_record(cb,set,cur);',' if(albedoView)contact_record(cb,set,cur);')
s=s.replace('static struct { u64 address, write; VkImage image; } cap_material;', 'static struct {u64 address;VkImage image;u32 tic[8],tsc[8];} worldProbe;\nstatic struct { u64 address, write; VkImage image; } cap_material;')
s=s.replace(' cap_material.image=VK_NULL_HANDLE;', ' cap_material.image=VK_NULL_HANDLE;worldProbe.image=VK_NULL_HANDLE;')
s=s.replace('  if(fs->si.tex[i].cbuf_off==0x32)z=s;', """  if(s && fs->si.tex[i].cbuf_off==0x36 && vk_tex_view_type(&fs->si.tex[i])==VK_IMAGE_VIEW_TYPE_CUBE){
       worldProbe.address=s->gva;worldProbe.image=s->img;
       memcpy(worldProbe.tic,tex[i].tic,sizeof worldProbe.tic);memcpy(worldProbe.tsc,tex[i].tsc,sizeof worldProbe.tsc);
   }
   if(fs->si.tex[i].cbuf_off==0x32)z=s;""")
s=s.replace('VkDescriptorSetLayoutBinding b[6];for(int i=0;i<6;i++)','VkDescriptorSetLayoutBinding b[7];for(int i=0;i<7;i++)').replace('i==2||i==5','i==2||i>=5').replace('.bindingCount=6,.pBindings=b','.bindingCount=7,.pBindings=b').replace('VK_SHADER_STAGE_COMPUTE_BIT,0,80','VK_SHADER_STAGE_COMPUTE_BIT,0,112')
s=s.replace(' VkImageView views[]={zv,motion->view,cv,color->view,depth->view,albedoView?albedoView:cv};VkDescriptorImageInfo ii[6];VkWriteDescriptorSet wr[6];', """ VkImageView probeView=VK_NULL_HANDLE;
 if(worldProbe.image){
     for(Surf *probe=vk_surf_first(worldProbe.address);probe;probe=probe->next){
         if(probe->img!=worldProbe.image||probe->gva!=worldProbe.address)continue;
         probeView=vk_tex_bind_view(probe,VK_IMAGE_VIEW_TYPE_CUBE,worldProbe.tic,worldProbe.tsc,0);
         if(probeView){vk_surf_touch(probe);vk_surf_use(cb,probe,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);}
         break;
     }
 }
 VkImageView views[]={zv,motion->view,cv,color->view,depth->view,albedoView?albedoView:cv,probeView};VkDescriptorImageInfo ii[7];VkWriteDescriptorSet wr[7];""")
s=s.replace('for(int i=0;i<6;i++){int sampled','for(int i=0;i<(probeView?7:6);i++){int sampled').replace('vkUpdateDescriptorSets(g_vk.dev,6,wr','vkUpdateDescriptorSets(g_vk.dev,probeView?7:6,wr').replace('if(albedoView)contact_record','if(probeView && albedoView)contact_record')
s=s.replace('} worldProbe;', '} worldProbe,worldDiffuse;').replace('worldProbe.image=VK_NULL_HANDLE;','worldProbe.image=VK_NULL_HANDLE;worldDiffuse.image=VK_NULL_HANDLE;')
s=s.replace('   if(fs->si.tex[i].cbuf_off==0x32)z=s;', """   if(s && fs->si.tex[i].cbuf_off==0x34 && vk_tex_view_type(&fs->si.tex[i])==VK_IMAGE_VIEW_TYPE_CUBE){
       worldDiffuse.address=s->gva;worldDiffuse.image=s->img;
       memcpy(worldDiffuse.tic,tex[i].tic,sizeof worldDiffuse.tic);memcpy(worldDiffuse.tsc,tex[i].tsc,sizeof worldDiffuse.tsc);
   }
   if(fs->si.tex[i].cbuf_off==0x32)z=s;""")
start=s.index(' VkImageView probeView=VK_NULL_HANDLE;')
end=s.index(' VkImageView views[]={zv', start)
s=s[:end]+s[start:end].replace('probeView','diffuseView').replace('worldProbe','worldDiffuse')+s[end:]
s=s.replace('VkDescriptorSetLayoutBinding b[7];for(int i=0;i<7;i++)','VkDescriptorSetLayoutBinding b[8];for(int i=0;i<8;i++)').replace('.bindingCount=7,.pBindings=b','.bindingCount=8,.pBindings=b')
s=s.replace('cv,probeView};VkDescriptorImageInfo ii[7];VkWriteDescriptorSet wr[7];','cv,probeView,diffuseView};VkDescriptorImageInfo ii[8];VkWriteDescriptorSet wr[8];')
s=s.replace('probeView?7:6','probeView && diffuseView?8:6').replace('if(probeView && albedoView)','if(probeView && diffuseView && albedoView)')
(tmp/'render.c').write_text(s)
db=json.loads((build/'compile_commands.json').read_text())
e=next(e for e in db if e['file'].endswith('/nx/gpu/vk/render.c'))
args=[v.strip('"') for v in shlex.split(e['command'],posix=False)]
args[args.index('-o')+1]=str(tmp/'render.obj');args[-1]=str(tmp/'render.c')
subprocess.run(args+['-I'+str(ups)],cwd=build,check=True)
s=(w/'build-merged.py').read_text().split('args=json.loads((fix/',1)[1]
s='args=json.loads((fix/'+s
s=s.replace("str(tmp/'query.obj')","str(w/'query.obj')").replace("str(ups/f) for f","str((w/'pt' if f=='device.obj' else ups)/f) for f").replace('village-v3-4k-runtime','world-pt-preview-runtime').replace('Arceus-Village-v3-DLSS-4K.exe','Arceus-World-PT-Preview-4K.exe').replace('current-village-v3-DLSS-4K-test','current-world-pt-preview')
exec(s)



