typedef struct {
    float camera[4],axis[3][4],viewport[4],jitter[2];
    u64 scene;
    u32 enabled,hits[16];
} ForwardParams;
_Static_assert(offsetof(ForwardParams,scene)==88 && offsetof(ForwardParams,hits)==100,"forward reflection layout");
static struct {
    ContactBuffer buffer;
    ForwardParams *mapped;
    u32 frame;
    bool attempted,used;
} forwardScene;

static void forward_prepare(VkCommandBuffer cb,const WorldPush *pc){
    if(!forwardScene.attempted){
        forwardScene.attempted=true;
        if(!contact_buffer(&forwardScene.buffer,sizeof(ForwardParams),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,true) ||
           vkMapMemory(g_vk.dev,forwardScene.buffer.memory,0,VK_WHOLE_SIZE,0,(void**)&forwardScene.mapped)!=VK_SUCCESS)return;
        memset(forwardScene.mapped,0,sizeof(ForwardParams));
    }
    if(!forwardScene.mapped)return;
    ForwardParams params={.scene=pc->scene,.enabled=1};
    memcpy(params.camera,pc->camera,sizeof params.camera);memcpy(params.axis,pc->axes,sizeof params.axis);
    memcpy(params.viewport,land.gpu.viewport,sizeof params.viewport);memcpy(params.jitter,land.gpu.jitter,sizeof params.jitter);
    contact_barrier(cb,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdUpdateBuffer(cb,forwardScene.buffer.handle,0,offsetof(ForwardParams,hits),&params);
    contact_barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    forwardScene.frame=g_vk_present;
    forwardScene.used=false;
}
static bool forward_bind(Surf *target,DescWrites *d){
    bool live=forwardScene.mapped && forwardScene.frame==g_vk_present && forwardScene.used &&
        target && target->img==capForwardImage && target->gva==cap_color;
    VkBuffer buffer;u64 offset=0;
    if(live)buffer=forwardScene.buffer.handle;
    else if(!vk_buf_copy(0,sizeof(ForwardParams),&buffer,&offset))return false;
    dw_buffer(d,VK_NULL_HANDLE,SNX_VKB_SSBO+15,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,buffer,offset,sizeof(ForwardParams));
    for(u32 i=0;i<2;i++){
        THist *image=&th[0][i];
        VkImageView view=live?image->view:vk_null_view(VK_IMAGE_VIEW_TYPE_2D,0);
        if(!view || !particle_sampler())return false;
        u32 bind=SNX_VKB_TEX+SH_MAXTEX-2+i;
        d->ii[bind]=(VkDescriptorImageInfo){particles.sampler,view,live?image->layout:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        d->w[d->n++]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstBinding=bind,
            .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&d->ii[bind]};
    }
    return true;
}
