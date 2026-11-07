static struct {
    ContactBuffer transfer;
    THist volume;
    VkPipeline pipeline;
    VkSampler sampler;
    u32 seen,frame;
    bool attempted,ready;
} particles;

#ifdef PARTICLE_WITNESS
static ContactBuffer smokeWitnessBuffer;
static u32 *smokeCounts;
static VkBuffer particle_witness_buffer(void){
    if(smokeCounts)return smokeWitnessBuffer.handle;
    if(!contact_buffer(&smokeWitnessBuffer,24,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true))return VK_NULL_HANDLE;
    if(vkMapMemory(g_vk.dev,smokeWitnessBuffer.memory,0,24,0,(void**)&smokeCounts)!=VK_SUCCESS)return VK_NULL_HANDLE;
    memset(smokeCounts,0,24);
    return smokeWitnessBuffer.handle;
}
#endif

static bool particle_sampler(void){
    if(particles.sampler)return true;
    VkSamplerCreateInfo sc={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_LINEAR,.minFilter=VK_FILTER_LINEAR,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    return vkCreateSampler(g_vk.dev,&sc,NULL,&particles.sampler)==VK_SUCCESS;
}

static bool particle_init(void){
    if(particles.attempted)return particles.ready;
    particles.attempted=true;
    const char *setting=getenv("ARCEUS_PT_PARTICLE_LIGHT");
    if(setting && !strcmp(setting,"0"))return false;
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_3D,.format=VK_FORMAT_R8G8B8A8_UNORM,
        .extent={60,34,44},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    if(!vk_image_create(&ci,&particles.volume.im,&particles.volume.mem))return false;
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=particles.volume.im,.viewType=VK_IMAGE_VIEW_TYPE_3D,
        .format=ci.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    if(vkCreateImageView(g_vk.dev,&vi,NULL,&particles.volume.view)!=VK_SUCCESS ||
       !contact_buffer(&particles.transfer,60*34*44*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false) ||
       !particle_sampler() || !contact_pipeline(particleGrid_words,sizeof particleGrid_words,&particles.pipeline)){
        vkDestroyImageView(g_vk.dev,particles.volume.view,NULL);
        vk_image_destroy(particles.volume.im,particles.volume.mem);
        if(particles.transfer.handle){
            vkDestroyBuffer(g_vk.dev,particles.transfer.handle,NULL);
            vkFreeMemory(g_vk.dev,particles.transfer.memory,NULL);
        }
        return false;
    }
    particles.ready=true;
    return true;
}

static bool particle_binding(Surf *target,VkDescriptorImageInfo *info){
    if(target)particles.seen=g_vk_present;
    bool live=particles.ready && particles.frame==g_vk_present && target && target->gva==cap_color &&
              target->f.vk==VK_FORMAT_B10G11R11_UFLOAT_PACK32 && target->sw==1920 && target->sh==1080;
    if(!particle_sampler())return false;
    VkImageView view=live?particles.volume.view:vk_null_view(VK_IMAGE_VIEW_TYPE_3D,0);
    if(!view)return false;
    *info=(VkDescriptorImageInfo){particles.sampler,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    if(live && getenv("ARCEUS_RT_VERIFY")){
        static u32 last;
        if(last!=g_vk_present && g_vk_present%120==0){last=g_vk_present;LOG("world-pt: smoke consumes traced lighting at present %u\n",last);}
    }
    return true;
}

static void particle_dispatch(VkCommandBuffer cb){
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,particles.pipeline);
    vkCmdDispatch(cb,8,5,44);
    contact_barrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    THist *vol=&particles.volume;
    VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_READ_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout=vol->layout,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=vol->im,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={60,34,44}};
    vkCmdCopyBufferToImage(cb,particles.transfer.handle,vol->im,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);
    vol->layout=barrier.newLayout;particles.frame=g_vk_present;
}
static void particle_capture_previous(void){
    static bool done;
    const char *path=getenv("ARCEUS_QA_PARTICLE_GRID");
    if(!path || done || !particles.ready || g_vk_present!=1961 || particles.frame!=1960)return;
    done=true;
    const VkDeviceSize bytes=60*34*44*4;
    VkBuffer dest;
    const void *mapped=vk_readback_buffer(bytes,&dest);
    if(!mapped)return;
    VkCommandBuffer cb=vk_cmd();vk_render_end();vk_rec_sync();
    contact_barrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferCopy copy={.size=bytes};
    vkCmdCopyBuffer(cb,particles.transfer.handle,dest,1,&copy);
    contact_barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
#ifdef PARTICLE_WITNESS
    contact_barrier(cb,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
#endif
    vk_finish();vk_readback_ready();
#ifdef PARTICLE_WITNESS
    if(smokeCounts)LOG("world-pt: smoke fragment witness coverage=%u attenuated=%u aboveTenPercent=%u\n",
        smokeCounts[0],smokeCounts[1],smokeCounts[2]);
    if(smokeCounts)LOG("world-pt: village smoke fragment witness coverage=%u attenuated=%u aboveTenPercent=%u\n",
        smokeCounts[3],smokeCounts[4],smokeCounts[5]);
#endif
    FILE *out=fopen(path,"wb");
    if(out){fwrite(mapped,1,(size_t)bytes,out);fclose(out);}
}
