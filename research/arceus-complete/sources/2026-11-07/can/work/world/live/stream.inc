enum { worldMaxVertices=1500000 };
static struct {
    ContactBuffer clip;
    u32 frame, used, skipped;
    int attempted;
    u64 albedoAddress;
} world;
static void world_frame(void){
    if(!arceus_contact_supported || !g_vk.has_xfb)return;
    if(!world.attempted){
        world.attempted=1;
        if(!contact_buffer(&world.clip,worldMaxVertices*16ull,
            VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,false))return;
        world.frame=~0u;
    }
    if(!world.clip.address || world.frame==g_vk_present)return;
    vk_render_end();
    VkCommandBuffer cb=vk_cmd();
    vk_rec_sync();
    contact_barrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdFillBuffer(cb,world.clip.handle,0,VK_WHOLE_SIZE,0);
    contact_barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT);
    world.frame=g_vk_present;world.used=0;world.skipped=0;world.albedoAddress=0;
}
static int world_capture_begin(VkCommandBuffer cb,u32 count,Surf *albedo){
    if(!world.clip.address || world.frame!=g_vk_present || !count)return 0;
    if(count>worldMaxVertices-world.used){world.skipped++;return 0;}
    VkDeviceSize offset=16ull*world.used,bytes=16ull*count;
    rec_CmdBindTransformFeedbackBuffersEXT(cb,0,1,&world.clip.handle,&offset,&bytes);
    rec_CmdBeginTransformFeedbackEXT(cb,0,0,NULL,NULL);
    world.used+=count;
    if(albedo && albedo->f.vk==VK_FORMAT_R8G8B8A8_SRGB)world.albedoAddress=albedo->gva;
    return 1;
}
