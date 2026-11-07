#define AUDIT_WIDTH 1920
#define AUDIT_ROWS 17
#define AUDIT_MAX 512
static struct {
    ContactBuffer rows;
    struct {u64 shader;u32 draw,blend;} pass[AUDIT_MAX];
    u32 count;
} overlayAudit;

static void overlay_audit(Surf *color,VkSh *shader,bool blend){
    if(!shader || !color || color->gva!=cap_color || cap_frame!=1960 || g_vk_present!=1960 ||
       color->f.vk!=VK_FORMAT_B10G11R11_UFLOAT_PACK32 || color->sw!=AUDIT_WIDTH || color->sh!=1080 ||
       overlayAudit.count>=AUDIT_MAX || !getenv("ARCEUS_QA_PASS_ROWS"))return;
    const u32 stride=AUDIT_WIDTH*AUDIT_ROWS*4;
    if(!overlayAudit.rows.handle && !contact_buffer(&overlayAudit.rows,(u64)stride*AUDIT_MAX,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true))return;
    u32 slot=overlayAudit.count++;
    overlayAudit.pass[slot].shader=shader->hash;
    overlayAudit.pass[slot].draw=(u32)g_vk_drawn;
    overlayAudit.pass[slot].blend=blend;
    VkCommandBuffer cb=vk_cmd();
    vk_render_end();
    vk_surf_use(cb,color,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_READ_BIT);
    vk_rec_sync();
    VkBufferImageCopy strips[AUDIT_ROWS];
    for(u32 i=0;i<AUDIT_ROWS;i++)strips[i]=(VkBufferImageCopy){
        .bufferOffset=(u64)slot*stride+i*AUDIT_WIDTH*4,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
        .imageOffset={0,(int)(32+63*i),0},.imageExtent={AUDIT_WIDTH,1,1}};
    vkCmdCopyImageToBuffer(cb,color->img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,overlayAudit.rows.handle,AUDIT_ROWS,strips);
    vk_render_forget_state();
}

static void overlay_audit_save(void){
    if(g_vk_present!=1961 || !overlayAudit.count)return;
    VkCommandBuffer cb=vk_cmd();
    vk_render_end();vk_rec_sync();
    contact_barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
    vk_finish();
    contact_wait_readback();
    VkDeviceSize bytes=overlayAudit.count*(VkDeviceSize)AUDIT_WIDTH*AUDIT_ROWS*4;
    void *data=NULL;
    if(vkMapMemory(g_vk.dev,overlayAudit.rows.memory,0,bytes,0,&data)==VK_SUCCESS){
        FILE *out=fopen(getenv("ARCEUS_QA_PASS_ROWS"),"wb");
        if(out){
            fwrite(&overlayAudit.count,4,1,out);
            fwrite(overlayAudit.pass,sizeof(overlayAudit.pass[0]),overlayAudit.count,out);
            fwrite(data,1,(size_t)bytes,out);
            fclose(out);
        }
        vkUnmapMemory(g_vk.dev,overlayAudit.rows.memory);
    }
    vkDestroyBuffer(g_vk.dev,overlayAudit.rows.handle,NULL);
    vkFreeMemory(g_vk.dev,overlayAudit.rows.memory,NULL);
    memset(&overlayAudit,0,sizeof overlayAudit);
}
