static struct {ContactBuffer buffer;u8 *mapped;bool saved;u32 frame;} materialAudit;
static void material_audit_save(void){
    if(materialAudit.saved || !materialAudit.frame || g_vk_present<=materialAudit.frame+1)return;
    contact_wait_readback();
    const char *path=getenv("ARCEUS_QA_GBUFFER");
    FILE *f=path?fopen(path,"wb"):NULL;
    if(f){
        fwrite(materialAudit.mapped,1920ull*1080*4,3,f);fclose(f);
    }
    materialAudit.saved=true;
}
static void material_audit_capture(VkCommandBuffer cb,Surf *normal,Surf *surface){
    if(materialAudit.frame || !getenv("ARCEUS_QA_GBUFFER") || g_vk_present!=rr_audit_frame())return;
    Surf *albedo=lighting_surface(&lightingAlbedo,1920,1080);
    if(!albedo || !normal || !surface)return;
    Surf *images[]={albedo,normal,surface};
    const u64 bytes=1920ull*1080*4;
    if(!contact_buffer(&materialAudit.buffer,bytes*3,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true) ||
       vkMapMemory(g_vk.dev,materialAudit.buffer.memory,0,VK_WHOLE_SIZE,0,(void**)&materialAudit.mapped))return;
    vk_rec_sync();
    for(u32 i=0;i<3;i++){
        vk_surf_use(cb,images[i],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region={.bufferOffset=bytes*i,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={1920,1080,1}};
        rec_CmdCopyImageToBuffer(cb,images[i]->img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,materialAudit.buffer.handle,1,&region);
        vk_surf_use(cb,images[i],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    vk_rec_sync();
    contact_barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
    materialAudit.frame=g_vk_present;
}
