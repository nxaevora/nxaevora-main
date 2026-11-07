static bool rr_requested(void){
    static int requested=-1;
    if(requested<0){
        const char *mode=getenv("ARCEUS_DLSS_RR");
        requested=mode && !strcmp(mode,"1");
    }
    return requested!=0;
}
static bool rr_prepare(u32 width,u32 height){
    if(rr.attempted)return rr.ready;
    rr.attempted=true;
    if(!contact_pipeline(rrContact_words,sizeof rrContact_words,&rr.pipeline))return false;
    for(unsigned i=0;i<RR_GUIDE_COUNT;i++){
        VkFormat format=i==RR_HIT_DISTANCE?VK_FORMAT_R32_SFLOAT:VK_FORMAT_R16G16B16A16_SFLOAT;
        if(!denoise_image(&rr.images[i],format,width,height,VK_IMAGE_USAGE_TRANSFER_DST_BIT))goto failed;
        rr.guides.image[i]=rr.images[i].im;
        rr.guides.view[i]=rr.images[i].view;
    }
    if(!contact_buffer(&rr.buffer,(VkDeviceSize)width*height*28,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,false))goto failed;
    rr.ready=true;
    return true;
failed:
    for(unsigned i=0;i<RR_GUIDE_COUNT;i++){
        if(rr.images[i].view)vkDestroyImageView(g_vk.dev,rr.images[i].view,NULL);
        if(rr.images[i].im)vk_image_destroy(rr.images[i].im,rr.images[i].mem);
    }
    vkDestroyPipeline(g_vk.dev,rr.pipeline,NULL);
    LOG("arceus-rr: guide allocation unavailable, using standard denoising\n");
    return false;
}
static void rr_transition(VkCommandBuffer cb,THist *image,VkImageLayout layout){
    // Copy and NGX commands here are recorded directly after vk_rec_sync.
    flow_image_layout(cb,image->im,image->layout,layout);
    image->layout=layout;
}
static void rr_copy(VkCommandBuffer cb,u32 width,u32 height){
    contact_barrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    for(unsigned i=0;i<RR_GUIDE_COUNT;i++){
        THist *img=&rr.images[i];
        rr_transition(cb,img,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy={.bufferOffset=(VkDeviceSize)i*width*height*8,
            .imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={width,height,1}};
        vkCmdCopyBufferToImage(cb,rr.buffer.handle,img->im,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        rr_transition(cb,img,VK_IMAGE_LAYOUT_GENERAL);
    }
}

// Read back one completed village frame when the validation harness asks for guides.
// Do this before recording the next resolve, since vk_finish ends the command buffer.
static void rr_capture_previous(void){
    static bool captured;
    if(captured || !rr.ready || !rr.writing || g_vk_present!=1961 || tlast!=1960)return;
    const char *path=getenv("ARCEUS_QA_RR_CAPTURE");
    if(!path)return;
    captured=true;
    const u32 w=1920,h=1080;
    const bool overlays=rr.guides.beforeImage && rr.compositeForCapture;
    const u64 pixels=(u64)w*h,bytes=pixels*(overlays?48u:32u);
    VkBuffer buffer;
    const u8 *mapped=vk_readback_buffer(bytes,&buffer);
    if(!mapped)return;
    VkCommandBuffer cb=vk_cmd();
    vk_render_end();vk_rec_sync();
    u64 offset=0;
    for(unsigned i=0;i<=RR_GUIDE_COUNT;i++){
        THist *img=i==RR_GUIDE_COUNT?&th[0][1]:&rr.images[i];
        VkImageLayout layout=img->layout;
        rr_transition(cb,img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy={.bufferOffset=offset,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={w,h,1}};
        vkCmdCopyImageToBuffer(cb,img->im,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
        rr_transition(cb,img,layout);
        offset+=pixels*(i<RR_HIT_DISTANCE?8:4);
    }
    if(overlays){
        VkImage inputs[]={rr.guides.beforeImage,rr.compositeForCapture};
        for(unsigned i=0;i<2;i++){
            flow_image_layout(cb,inputs[i],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy copy={.bufferOffset=offset,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={w,h,1}};
            vkCmdCopyImageToBuffer(cb,inputs[i],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
            flow_image_layout(cb,inputs[i],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
            offset+=pixels*8;
        }
    }
    contact_barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
    vk_finish();vk_readback_ready();
    FILE *out=fopen(path,"wb");
    if(out){
        bool ok=fwrite(tprev,sizeof(float),16,out)==16 &&
            fwrite(rr.guides.worldToView,sizeof(float),16,out)==16 &&
            fwrite(rr.guides.viewToClip,sizeof(float),16,out)==16 &&
            fwrite(mapped,1,(size_t)bytes,out)==bytes;
        if(fclose(out))ok=false;
        LOG("arceus-rr: guide readback at frame 1960 %s\n",ok?"complete":"failed");
    }
}
