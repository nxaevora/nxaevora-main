from pathlib import Path
p=Path('work/v2')
def update(file,old,new,count=1):
    f=p/file;s=f.read_text();assert s.count(old)==count,(file,old,s.count(old))
    f.write_text(s.replace(old,new))
update('stream.inc','enum { worldMaxVertices','#include "material.inc"\nenum { worldMaxVertices')
update('stream.inc','worldMaxVertices*16ull','worldMaxVertices*32ull')
update('stream.inc','world.frame=~0u;', '''if(!contact_buffer(&drawTable,worldDrawLimit*sizeof(WorldDrawMaterial),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,false)){
            world.clip.address=0;return;
        }
        world.frame=~0u;''')
update('stream.inc','world.used=0;','world.used=0;drawCount=0;textureCount=0;')
update('stream.inc','Surf *albedo){','Surf *albedo,GpuChannel *ch,const VkSh *shader,const TexRes *bound){')
update('stream.inc','if(count>worldMaxVertices-world.used)','if(drawCount==worldDrawLimit || count>worldMaxVertices-world.used)')
update('stream.inc','16ull*world.used,bytes=16ull*count','32ull*world.used,bytes=32ull*count')
update('stream.inc','world.used+=count;','world.used+=count;material_range(ch,shader,bound,world.used/3);')
update('render.c','xfb.count[0]=4;xfb.stride[0]=16;','xfb.count[0]=6;xfb.stride[0]=32;xfb.layout[0][4]=32;xfb.layout[0][5]=33;')
update('render.c','world_capture_begin(cb,(u32)captureCount,rt[1])','world_capture_begin(cb,(u32)captureCount,rt[1],ch,fs,fres)')
update('render.c','VkDescriptorSetLayoutBinding b[8]','VkDescriptorSetLayoutBinding b[9]')
update('render.c','VkDescriptorSetLayoutCreateInfo dc={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=8', '''b[8]=(VkDescriptorSetLayoutBinding){8,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,128,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
 VkDescriptorSetLayoutCreateInfo dc={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=9''')
update('render.c',' if(probeView && diffuseView && albedoView)contact_record(cb,set,cur);',''' if(probeView && diffuseView && albedoView && drawCount){
     bind_world_textures(cb,set,albedoView,ro);
     contact_record(cb,set,cur);
 }''')
update('contact.h','rtTriangles*3*16,input','rtTriangles*6*16,input')
update('contact.h','u32 padding[2]','u64 draws')
update('contact.h','.clip=world.clip.address}', '.clip=world.clip.address,.draws=drawTable.address}')
update('contact.h','pc.count=','pc.count=',0)
update('contact.h','contact.bottom.range.primitiveCount=pc.count;','pc.axes[0][3]=(float)drawCount;\n    contact.bottom.range.primitiveCount=pc.count;')
update('contact.h','    vkCmdBindDescriptorSets(cb,','''    contact_barrier(cb,compute,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdUpdateBuffer(cb,drawTable.handle,0,drawCount*sizeof(WorldDrawMaterial),draws);
    contact_barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,compute,VK_ACCESS_SHADER_READ_BIT);
    vkCmdBindDescriptorSets(cb,''')
update('contact.h','contact.counters,16','contact.counters,32')
update('contact.h','contact.counters.memory,0,16','contact.counters.memory,0,32')
update('contact.h','mappedCounters,0,16','mappedCounters,0,32')
update('contact.h','secondaryHits=%u\\n','secondaryHits=%u texturedOffscreen=%u unresolved=%u textureSlots=%u\\n')
update('contact.h','contact.mappedCounters[3]);','contact.mappedCounters[3],contact.mappedCounters[4],contact.mappedCounters[5],textureCount);')
update('contact.comp','#extension GL_EXT_ray_query', '#extension GL_EXT_nonuniform_qualifier : require\n#extension GL_EXT_ray_query')
update('contact.comp','layout(binding=7)', 'layout(binding=8) uniform sampler2D drawTextures[128];\nlayout(binding=7)')
update('contact.comp','uint bounce;','uint bounce;uint textured;uint unresolved;')
update('contact.comp','layout(push_constant)', '''struct DrawMaterial {uint end;uint slot;vec2 reserved;vec4 transform;};
layout(buffer_reference,std430,buffer_reference_align=16) readonly buffer DrawTable {DrawMaterial draw[];};
layout(push_constant)''')
update('contact.comp','    Vertices clip;','    Vertices clip;DrawTable draws;')
update('contact.comp','vec4 a=u.clip.v[first],b=u.clip.v[first+1],c=u.clip.v[first+2];','''uint lo=0,hi=uint(u.axisX.w);
    while(lo<hi){uint mid=(lo+hi)/2;if(u.draws.draw[mid].end<=tri)lo=mid+1;else hi=mid;}
    DrawMaterial material=u.draws.draw[lo];
    vec4 a=u.clip.v[2*first],b=u.clip.v[2*(first+1)],c=u.clip.v[2*(first+2)];''')
update('contact.comp','vec4 p=u.clip.v[first+i];','vec4 p=u.clip.v[2*(first+i)];')
update('contact.comp','u.vertices.v[first+i]=vec4(point,1);','''u.vertices.v[first+i]=vec4(point,1);
        vec2 uv=(u.clip.v[2*(first+i)+1].xy-material.transform.zw)*material.transform.xy;
        u.vertices.v[1500000u+first+i]=vec4(uv.x,1.-uv.y,float(material.slot),0.);''')
update('contact.comp','out uint primitive){','out uint primitive,out vec2 bary){')
update('contact.comp','primitive=uint(rayQuery','bary=rayQueryGetIntersectionBarycentricsEXT(ray,true);\n    primitive=uint(rayQuery')
update('contact.comp','vec3 materialAt(vec3 hit,ivec2 size){','vec3 materialAt(vec3 hit,ivec2 size,uint primitive,vec2 bary,inout uint textured,inout uint unresolved){')
update('contact.comp','    return vec3(.36);','''    uint first=1500000u+3u*primitive;
    vec3 a=u.vertices.v[first].xyz;uint slot=uint(a.z);
    if(slot>0u && slot<128u){
        vec2 uv=a.xy*(1.-bary.x-bary.y)+u.vertices.v[first+1].xy*bary.x+u.vertices.v[first+2].xy*bary.y;
        if(!any(isnan(uv)) && !any(isinf(uv))){
            textured++;
            return clamp(textureLod(drawTextures[nonuniformEXT(slot)],uv,1.).rgb,vec3(0),vec3(.95));
        }
    }
    unresolved++;
    return vec3(.36);''')
update('contact.comp','uint secondary=0;','uint secondary=0,textured=0,unresolved=0;')
update('contact.comp','vec3 hit;uint primitive;', 'vec3 hit;uint primitive;vec2 bary;')
update('contact.comp','5000.,hit,primitive)','5000.,hit,primitive,bary)')
update('contact.comp','materialAt(hit,size)','materialAt(hit,size,primitive,bary,textured,unresolved)')
update('contact.comp','atomicAdd(c.bounce,secondary);','atomicAdd(c.bounce,secondary);atomicAdd(c.textured,textured);atomicAdd(c.unresolved,unresolved);')
