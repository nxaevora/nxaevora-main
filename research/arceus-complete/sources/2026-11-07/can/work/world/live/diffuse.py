from pathlib import Path
p=Path('work/world/live')
shader=(p/'contact.comp').read_text()
shader=shader.replace('layout(binding=6) uniform samplerCube environmentMap;', 'layout(binding=6) uniform samplerCube environmentMap;\nlayout(binding=7) uniform samplerCube diffuseProbe;')
shader=shader.replace('        // Sample the environment directly.', '''        vec3 worldNormal=normal.x*u.axisX.xyz+normal.y*u.axisY.xyz+normal.z*u.axisZ.xyz;
        // The guest diffuse probe contains local irradiance, including light in enclosed rooms.
        // Use it as a finite-length termination estimate at surfaces, weighted across path depths.
        vec3 localLight=max(textureLod(diffuseProbe,normalize(worldNormal),0.).rgb,vec3(0));
        radiance+=throughput*localLight*(depth==0u?.72:depth==1u?.20:.08);
        // Sample the environment directly.''')
(p/'contact.comp').write_text(shader)
base=(p/'build.py').read_text()
index=base.index("(tmp/'render.c').write_text(s)")
base=base[:index]+'''s=s.replace('} worldProbe;', '} worldProbe,worldDiffuse;').replace('worldProbe.image=VK_NULL_HANDLE;','worldProbe.image=VK_NULL_HANDLE;worldDiffuse.image=VK_NULL_HANDLE;')
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
'''+base[index:]
(p/'build.py').write_text(base)
