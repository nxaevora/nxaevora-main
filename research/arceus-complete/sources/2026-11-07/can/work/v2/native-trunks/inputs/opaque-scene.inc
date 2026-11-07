typedef struct {
    VkFsr2Frame f;
    FlowParams flow;
    VkImageView albedo,materialView;
    u64 materialAddress;
    bool dl,traced,guides,hasFlow,materialHistory,filtered,composed;
} SceneResolve;
#include "overlay-mask.inc"

static u64 previous_color,previous_color_write,previous_depth_write;
static bool upscale_disabled,previousRR;
static struct {
    THist combined;
    SceneResolve input;
    VkImage color,depth;
    u64 write;
    u32 frame;
    bool attempted;
} opaque;

static bool opaque_image_ready(u32 w,u32 h){
    if(!opaque.attempted){
        opaque.attempted=true;
        VkFormatProperties fp;
        const VkFormatFeatureFlags required=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT;
        vkGetPhysicalDeviceFormatProperties(g_vk.phys,VK_FORMAT_B10G11R11_UFLOAT_PACK32,&fp);
        if((fp.optimalTilingFeatures&required)!=required)return false;
        vkGetPhysicalDeviceFormatProperties(g_vk.phys,VK_FORMAT_R16G16B16A16_SFLOAT,&fp);
        if((fp.optimalTilingFeatures&required)!=required)return false;
        if(!denoise_image(&opaque.combined,VK_FORMAT_R16G16B16A16_SFLOAT,w,h,VK_IMAGE_USAGE_TRANSFER_DST_BIT))return false;
    }
    return opaque.combined.view!=VK_NULL_HANDLE;
}

static void scene_filter(VkCommandBuffer cb,SceneResolve *input){
    if(input->traced && !input->filtered){
        input->filtered=denoise_record(cb,&input->f,input->albedo,input->materialView,input->flow,
            input->dl?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

static void scene_blit(VkCommandBuffer cb,VkImage source,VkImage dest,u32 w,u32 h){
    VkImageBlit region={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
        .dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.srcOffsets={{0,0,0},{(int)w,(int)h,1}},
        .dstOffsets={{0,0,0},{(int)w,(int)h,1}}};
    rec_CmdBlitImage(cb,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dest,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region,VK_FILTER_NEAREST);
}

static void opaque_write(VkCommandBuffer cb,Surf *col,Surf *z,const SceneResolve *input){
    THist *source=(input->f.color==th[0][0].im)?&th[0][0]:&denoiser.colors[denoiser.index];
    VkImageLayout previous=source->layout;
    tuse(cb,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vk_surf_use(cb,col,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT);
    scene_blit(cb,source->im,col->img,col->sw,col->sh);
    target_written(col);
    tuse(cb,source,previous);
    if(input->guides || overlay_mask_init(col->sw,col->sh)){
        // Both sides of the comparison pass through the guest packed HDR format.
        // Preserve the standard denoiser's high-precision history in its own image.
        THist *reference=&th[0][0];
        vk_surf_use(cb,col,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_READ_BIT);
        tuse(cb,reference,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        scene_blit(cb,col->img,reference->im,col->sw,col->sh);
        tuse(cb,reference,input->dl?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    opaque.input=*input;opaque.frame=g_vk_present;
    opaque.color=col->img;opaque.depth=z->img;opaque.write=col->wseq;
    vk_render_forget_state();
}

static void scene_finish(VkCommandBuffer cb,Surf *col,Surf *z,SceneResolve *input){
    bool rrUsed=false;
    VkFsr2Frame *f=&input->f;
    if(input->guides){
        rr_copy(cb,f->w,f->h);
        VkFsr2Frame raw=*f;
        raw.reset|=!previousRR;
        rrUsed=vk_rr_evaluate(cb,&raw,&rr.guides);
        if(!rrUsed){
            contact_barrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
            f->reset=true;
        }
    }
    if(previousRR && !rrUsed)f->reset=true;
    if(rrUsed!=previousRR)LOG("arceus-rr: %s at present %u\n",rrUsed?"evaluating raw radiance with four guides":"standard path resumed",g_vk_present);
    if(rrUsed && getenv("ARCEUS_RT_VERIFY") && g_vk_present%120==0)LOG("arceus-rr: evaluated raw radiance at present %u\n",g_vk_present);
    previousRR=rrUsed;
    // Once the guest has drawn overlays, their material attachments may have been reused.
    // An RR evaluate failure gets one SR reset here, then standard filtering on the next opaque pass.
    if(!rrUsed && !input->composed)scene_filter(cb,input);
    world_mark(cb,8);
    THist *motion=&th[1][0],*dst=&th[1][1];
    if(!input->dl){
        flow_image_layout(cb,motion->im,motion->layout,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        motion->layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    bool ok=rrUsed || (input->dl?temporal_ngx_record(g_vk.dev,cb,f):vk_fsr3_record(g_vk.phys,g_vk.dev,cb,f));
    world_mark(cb,9);
    vk_render_forget_state();
    if(!ok){upscale_disabled=true;tready=0;WARN("arceus-upscaler: vendor dispatch failed; reverting to original lighting for session\n");return;}
    const char *backend=rrUsed?"DLSS RR":input->dl?"DLSS":"FSR 3.1.6";
    if(getenv("ARCEUS_RT_VERIFY") && g_vk_present%120==0)
        LOG("world-pt: backend=%s present=%u standardFilter=%u\n",backend,g_vk_present,input->filtered);
    const char *motionMode=input->materialHistory?"material+camera":input->hasFlow?"material-warmup":"camera-only";
    temporal_snapshot_set(rrUsed?"RR":input->dl?"DLSS":"FSR",motionMode,f->w,f->h,input->materialAddress,
        motion->im,motion->layout,input->hasFlow?&material_flow:NULL,&input->flow);
    tuse(cb,dst,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);up_override=dst->view;up_address=cap_color;
    LOG_LIMIT(32,"arceus-upscaler: %s pre-HUD %ux%u -> %ux%u frame=%u history=%u jitter=%g,%g motion=%s\n",
        backend,f->w,f->h,f->ow,f->oh,g_vk_present,!f->reset,-f->jx,-f->jy,motionMode);
    previous_color=cap_color;previous_color_write=col->wseq;previous_depth_write=z->wseq;
    memcpy(tprev,cap_matrix,sizeof tprev);tindex^=1;tready=1;tlast=g_vk_present;tzaddr=z->gva;
}

static void opaque_resolve(Surf *col,Surf *z){
    VkCommandBuffer cb=vk_cmd();vk_render_end();
    SceneResolve input=opaque.input;
    THist *combined=&opaque.combined;
    vk_surf_use(cb,col,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_READ_BIT);
    tuse(cb,combined,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    scene_blit(cb,col->img,combined->im,col->sw,col->sh);
    tuse(cb,combined,input.dl?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk_rec_sync();
    input.f.color=combined->im;input.f.color_view=combined->view;input.composed=true;
    if(!input.guides)overlay_mask_record(cb,&input);
    rr.compositeForCapture=input.guides?combined->im:VK_NULL_HANDLE;
    rr.guides.beforeImage=input.guides?th[0][0].im:VK_NULL_HANDLE;
    rr.guides.beforeView=input.guides?th[0][0].view:VK_NULL_HANDLE;
    if(getenv("ARCEUS_RT_VERIFY") && g_vk_present%120==0)
        LOG("world-pt: resolved guest overlay pass present=%u colorWritten=%u\n",g_vk_present,col->wseq!=opaque.write);
    scene_finish(cb,col,z,&input);
}
static void overlay_mask_capture_previous(void){
    static bool captured;
    const char *path=getenv("ARCEUS_QA_OVERLAY_MASK");
    if(!path || captured || g_vk_present!=1961 || tlast!=1960 || opaque.frame!=1960 ||
       !overlayMask.pipeline || opaque.input.guides || !opaque.combined.view)return;
    captured=true;
    THist *images[]={&th[0][0],&opaque.combined,&overlayMask.image};
    const u32 w=1920,h=1080;
    const u64 size=(u64)w*h*17;
    VkBuffer target;
    const void *mapped=vk_readback_buffer(size,&target);
    if(!mapped)return;
    VkCommandBuffer cb=vk_cmd();vk_render_end();vk_rec_sync();
    for(u32 i=0;i<3;i++){
        THist *image=images[i];
        VkImageLayout layout=image->layout;
        rr_transition(cb,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy={.bufferOffset=(u64)i*w*h*8,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={w,h,1}};
        vkCmdCopyImageToBuffer(cb,image->im,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,target,1,&copy);
        rr_transition(cb,image,layout);
    }
    contact_barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
    vk_finish();vk_readback_ready();
    FILE *file=fopen(path,"wb");
    if(file){
        bool ok=fwrite(mapped,1,(size_t)size,file)==size;
        if(fclose(file))ok=false;
        LOG("world-pt: overlay mask readback %s\n",ok?"complete":"failed");
    }
}
