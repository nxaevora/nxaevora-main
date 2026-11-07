
enum { worldMaxVertices=1500000 };
static struct {
    ContactBuffer clip;
    u32 frame, used, selected, skipped;
    int attempted;
    u64 albedoAddress;
    VkRect2D rect;
} world;
static void world_frame(void){
    if(!arceus_contact_supported || !g_vk.has_xfb)return;
    if(!world.attempted){
        world.attempted=1;
        if(!contact_buffer(&world.clip,worldMaxVertices*32ull,
            VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,false))return;
        if(!contact_buffer(&drawTable,worldDrawLimit*(sizeof(WorldDrawMaterial)+worldTintBytes)+sizeof(LandInfo)+sizeof mainLight+2*sizeof(VkDeviceAddress)+worldDrawLimit*(sizeof(float)+sizeof(u32))+sizeof(VkDeviceAddress)+(32+2048+worldMaxVertices/3)*sizeof(float),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,false)){
            world.clip.address=0;return;
        }
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
    world.frame=g_vk_present;world.used=0;world.selected=0;drawCount=0;captureDrawCount=0;textureCount=0;world.skipped=0;world.albedoAddress=0;
}
static int world_capture_begin(VkCommandBuffer cb,u32 count,Surf *albedo,Surf *depth,VkViewport vp,GpuChannel *ch,const VkSh *shader,const TexRes *bound){
    if(!world.clip.address || world.frame!=g_vk_present || !count)return 0;
    if(captureDrawCount>=worldDrawLimit-1 || count>worldMaxVertices-world.used){world.skipped++;return 0;}
    VkDeviceSize offset=32ull*world.used,bytes=32ull*count;
    rec_CmdBindTransformFeedbackBuffersEXT(cb,0,1,&world.clip.handle,&offset,&bytes);
    rec_CmdBeginTransformFeedbackEXT(cb,0,0,NULL,NULL);
    captureTargets[captureDrawCount]=(typeof(captureTargets[0])){albedo->img,depth->img,
        {{(i32)roundf(vp.x),(i32)roundf(fminf(vp.y,vp.y+vp.height))},
         {(u32)roundf(vp.width),(u32)roundf(fabsf(vp.height))}}};
    world.used+=count;material_range(ch,shader,bound,world.used/3);
    return 1;
}



// Match the G-buffer images actually read by this lighting pass. They can rotate
// on an area transition, before the first lighting draw identifies the new pair.
static void world_select(VkImage albedo,VkImage depth,u64 address){
    drawCount=0;world.selected=0;world.albedoAddress=0;
    if(world.frame!=g_vk_present || !albedo || !depth)return;
    world.rect=(VkRect2D){{0,0},{1920,1080}};
    u32 sourceStart=0,selected=0,largest=0;
    for(u32 i=0;i<captureDrawCount;i++){
        WorldDrawMaterial draw=capturedDraws[i];
        u32 sourceEnd=draw.end;
        if(captureTargets[i].albedo==albedo && captureTargets[i].depth==depth){
            u32 n=sourceEnd-sourceStart;
            VkRect2D r=captureTargets[i].rect;
            if(n>largest && r.offset.x>=0 && r.offset.y>=0 && r.extent.width && r.extent.height &&
               r.extent.width<=1920u-(u32)r.offset.x && r.extent.height<=1080u-(u32)r.offset.y &&
               r.offset.x<1920 && r.offset.y<1080){world.rect=r;largest=n;}
            selected+=n;
            draw.sourceStart=sourceStart;draw.end=selected;
            draws[drawCount++]=draw;
        }
        sourceStart=sourceEnd;
    }
    world.selected=selected*3u;
    if(selected)world.albedoAddress=address;
}
