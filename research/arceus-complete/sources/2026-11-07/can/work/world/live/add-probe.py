from pathlib import Path
p=Path('work/world/live')
s=(p/'contact.comp').read_text().replace('layout(binding=5) uniform sampler2D albedoMap;','layout(binding=5) uniform sampler2D albedoMap;\nlayout(binding=6) uniform samplerCube environmentMap;')
s=s.replace('vec4 sun;','vec4 axisX,axisY,axisZ;')
s=s.replace('radiance+=throughput*vec3(.38,.43,.51);break;', '''vec3 worldDir=direction.x*u.axisX.xyz+direction.y*u.axisY.xyz+direction.z*u.axisZ.xyz;
            radiance+=throughput*max(textureLod(environmentMap,worldDir,0.).rgb,vec3(0));break;''')
a=s.index('        float cosine=')
b=s.index('        direction=diffuse',a)
s=s[:a]+'''        // Sample the environment directly. A second cosine-weighted sample continues to other surfaces.
        vec3 lightDir=diffuse(normal,seed+depth*111u+53u);
        if(!shadowed(origin,lightDir,nearT)){
            vec3 worldDir=lightDir.x*u.axisX.xyz+lightDir.y*u.axisY.xyz+lightDir.z*u.axisZ.xyz;
            radiance+=throughput*max(textureLod(environmentMap,worldDir,0.).rgb,vec3(0));
        }
'''+s[b:]
s=s.replace('radiance+=throughput*max(textureLod(environmentMap,worldDir,0.).rgb,vec3(0));break;', 'if(depth==0u)radiance+=max(textureLod(environmentMap,worldDir,0.).rgb,vec3(0));break;')
(p/'contact.comp').write_text(s)
base=(p/'build.py').read_text()
index=base.index("(tmp/'contact.h').write_text(host)")
base=base[:index]+'''host=host.replace('float sun[4];','float axes[3][4];')
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
'''+base[index:]
index=base.index("(tmp/'render.c').write_text(s)")
base=base[:index]+'''s=s.replace('static struct { u64 address, write; VkImage image; } cap_material;', 'static struct {u64 address;VkImage image;u32 tic[8],tsc[8];} worldProbe;\\nstatic struct { u64 address, write; VkImage image; } cap_material;')
s=s.replace(' cap_material.image=VK_NULL_HANDLE;', ' cap_material.image=VK_NULL_HANDLE;worldProbe.image=VK_NULL_HANDLE;')
s=s.replace('  if(fs->si.tex[i].cbuf_off==0x32)z=s;', """  if(s && fs->si.tex[i].cbuf_off==0x24 && vk_tex_view_type(&fs->si.tex[i])==VK_IMAGE_VIEW_TYPE_CUBE){
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
'''+base[index:]
(p/'build.py').write_text(base)
