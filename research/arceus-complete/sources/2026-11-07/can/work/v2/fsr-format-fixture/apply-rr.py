from pathlib import Path
w=Path('work/v2')
def edit(name,a,b):
 p=w/name;s=p.read_text();assert s.count(a)==1,(name,a);p.write_text(s.replace(a,b))
edit('contact.comp','uvec2 primaryLight;};','uvec2 primaryLight;uvec2 rrData;};')
edit('contact.comp','    uint surfaceGuide=0u;','''    uint surfaceGuide=0u;
#ifdef RR_GUIDES
    vec3 rrDiffuse=vec3(0),rrSpecular=vec3(0);
    vec4 rrNormal=vec4(0,0,0,1);
    float rrDistance=0.;
#endif''')
edit('contact.comp','        // Sample the environment directly.', '''#ifdef RR_GUIDES
        if(depth==0u){
            rrNormal=vec4(normalize(worldNormal),clamp(rough,0.,1.));
            rrDiffuse=diffuseWeight;
            float grazing=pow(1.-clamp(dot(normal,-direction),0.,1.),5.);
            rrSpecular=f0+(max(vec3(1.-rough),f0)-f0)*grazing;
            vec3 reflectedHit;
            uint reflectedPrimitive;
            vec2 reflectedBary;
            if(traceScene(origin,reflect(direction,normal),nearT,5000.,reflectedHit,reflectedPrimitive,reflectedBary))
                rrDistance=length(reflectedHit-hit);
        }
#endif
        // Sample the environment directly.''')
edit('contact.comp','    if(written)imageStore', '''#ifdef RR_GUIDES
    Words guides=Words(u.draws.rrData);
    uint count=uint(size.x*size.y),index=uint(pixel.y*size.x+pixel.x);
    guides.v[2u*index]=packHalf2x16(rrDiffuse.rg);
    guides.v[2u*index+1u]=packHalf2x16(vec2(rrDiffuse.b,1));
    guides.v[2u*(count+index)]=packHalf2x16(rrSpecular.rg);
    guides.v[2u*(count+index)+1u]=packHalf2x16(vec2(rrSpecular.b,1));
    guides.v[2u*(2u*count+index)]=packHalf2x16(rrNormal.xy);
    guides.v[2u*(2u*count+index)+1u]=packHalf2x16(rrNormal.zw);
    guides.v[6u*count+index]=floatBitsToUint(rrDistance);
#endif
    if(written)imageStore''')
edit('stream.inc','sizeof mainLight+sizeof(VkDeviceAddress)','sizeof mainLight+2*sizeof(VkDeviceAddress)')
edit('contact.h','static struct {\n    int attempted, ready;', '''#include "rr-frame.h"
static struct {
    ContactBuffer buffer;
    VkPipeline pipeline;
    THist images[RR_GUIDE_COUNT];
    RrGuides guides;
    bool attempted,ready,writing;
} rr;
static struct {
    int attempted, ready;''')
edit('contact.h','    contact.bottom.ranges[0].primitiveCount=opaque;', '''    if(rr.writing){
        // Row-vector world-to-view with +Y up, +Z forward. The trace uses screen-down Y.
        for(unsigned axis=0;axis<3;axis++){
            float translation=0;
            for(unsigned j=0;j<3;j++){
                float basis=pc.axes[axis][j]*(axis==1?-1.f:1.f);
                rr.guides.worldToView[4*j+axis]=basis;
                translation-=pc.origin[j]*basis;
            }
            rr.guides.worldToView[12+axis]=translation;
        }
        rr.guides.worldToView[15]=1;
        rr.guides.viewToClip[0]=1.f/pc.camera[0]; rr.guides.viewToClip[5]=1.f/pc.camera[1];
        rr.guides.viewToClip[10]=-camera[15]/camera[11]; rr.guides.viewToClip[11]=1;
        rr.guides.viewToClip[14]=1.f/camera[11];
    }
    contact.bottom.ranges[0].primitiveCount=opaque;''')
edit('contact.h','    vkCmdFillBuffer(cb,drawTable.handle,worldDrawLimit', '''    if(rr.writing)vkCmdUpdateBuffer(cb,drawTable.handle,TERRAIN_INFO_OFFSET+sizeof land.gpu+sizeof mainLight+sizeof(VkDeviceAddress),
        sizeof rr.buffer.address,&rr.buffer.address);
    vkCmdFillBuffer(cb,drawTable.handle,worldDrawLimit''')
edit('contact.h','COMPUTE,contact.shadePipeline);','COMPUTE,rr.writing?rr.pipeline:contact.shadePipeline);')
edit('denoise.inc','static bool denoise_image(THist *image, VkFormat format,u32 w,u32 h){','static bool denoise_image(THist *image, VkFormat format,u32 w,u32 h,VkImageUsageFlags extra){')
edit('denoise.inc','VK_IMAGE_USAGE_TRANSFER_SRC_BIT};','VK_IMAGE_USAGE_TRANSFER_SRC_BIT|extra};')
edit('denoise.inc','R16G16B16A16_SFLOAT,w,h)','R16G16B16A16_SFLOAT,w,h,0)')
edit('denoise.inc','R32G32_UINT,w,h)','R32G32_UINT,w,h,0)')
edit('render.c','#include "denoise.inc"','#include "denoise.inc"\n#include "rr-render.inc"')
edit('render.c',' bool traced=probeView', ''' bool dl=vk_dlaa_wanted();
 static bool initialized;
 if(dl && !initialized){initialized=true;vk_dlaa_init(g_vk.instance,g_vk.phys,g_vk.dev);}
 const bool requested=rr_requested();
 if(requested && dl && !vk_dlaa_available())dl=false;
 rr.writing=requested && dl && surfaceInputs && probeView && diffuseView && albedoView && drawCount &&
     vk_rr_available() && rr_prepare(w,h);
 if(rr.writing)contact_barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT,
     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
 bool traced=probeView''')
edit('render.c',' bool dl=vk_dlaa_wanted();\n VkImageLayout vendor_layout',' VkImageLayout vendor_layout')
edit('render.c',' if(traced)denoise_record', ''' static bool previousRR;
 bool rrUsed=false;
 if(traced && rr.writing){
     rr_copy(cb,w,h);
     VkFsr2Frame raw=f;
     raw.reset|=!previousRR;
     rrUsed=vk_rr_evaluate(cb,&raw,&rr.guides);
     if(!rrUsed){
         contact_barrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,
             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
         f.reset=true;
     }
 }
 if(previousRR && !rrUsed)f.reset=true;
 if(rrUsed!=previousRR)LOG("arceus-rr: %s at present %u\\n",rrUsed?"evaluating raw radiance with four guides":"standard path resumed",g_vk_present);
 previousRR=rrUsed;
 if(traced && !rrUsed)denoise_record''')
edit('render.c',' static int init;if(dl&&!init){init=1;vk_dlaa_init(g_vk.instance,g_vk.phys,g_vk.dev);}\n bool ok=dl?temporal_ngx_record(g_vk.dev,cb,&f):vk_fsr3_record(g_vk.phys,g_vk.dev,cb,&f);',' bool ok=rrUsed || (dl?temporal_ngx_record(g_vk.dev,cb,&f):vk_fsr3_record(g_vk.phys,g_vk.dev,cb,&f));')
edit('render.c','temporal_snapshot_set(dl?"DLSS":"FSR"','temporal_snapshot_set(rrUsed?"RR":dl?"DLSS":"FSR"')
edit('render.c','motion=%s\\n",dl?"DLSS":"FSR 3.1.6"','motion=%s\\n",rrUsed?"DLSS RR":dl?"DLSS":"FSR 3.1.6"')
