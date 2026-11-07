static struct {
    ContactBuffer buffer;
    u8 *mapped;
    u32 frame,count,draws,slots,serial;
    float axes[3][4],origin[4];
    WorldDrawMaterial materials[worldDrawLimit];
    struct {u64 hash;u32 w,h;} images[worldTextureSlots];
} geometryCapture;

static void capture_geometry(VkCommandBuffer cb,const float axes[3][4],const float origin[4]){
    const char *prefix=getenv("ARCEUS_STATIC_CAPTURE");
    if(!prefix)return;
    if(geometryCapture.frame && g_vk_present>geometryCapture.frame+3){
        contact_wait_readback();
        char path[1024];
        snprintf(path,sizeof path,"%s-%05u.bin",prefix,geometryCapture.frame);
        FILE *f=fopen(path,"wb");
        if(f){
            u32 header[]={0x31504353,geometryCapture.count,geometryCapture.draws,geometryCapture.slots};
            fwrite(header,sizeof header,1,f);fwrite(geometryCapture.axes,48,1,f);fwrite(geometryCapture.origin,16,1,f);
            fwrite(geometryCapture.materials,sizeof *draws,geometryCapture.draws,f);
            fwrite(geometryCapture.images,sizeof *geometryCapture.images,geometryCapture.slots,f);
            for(u32 stream=0;stream<2;stream++){
                const u8 *data=geometryCapture.mapped+16ull*stream*geometryCapture.count;
                u32 start=0;
                for(u32 j=0;j<geometryCapture.draws;j++){
                    WorldDrawMaterial *draw=&geometryCapture.materials[j];
                    fwrite(data+48ull*draw->base,48,draw->end-start,f);start=draw->end;
                }
            }
            fclose(f);
        }
        geometryCapture.frame=0;
    }
    bool interior=getenv("ARCEUS_CAPTURE_INTERIOR")!=NULL;
    bool selected=interior ? ((!land.gpu.enabled || (land.request&255)==13) && g_vk_present>=300 && g_vk_present<=700 && g_vk_present%75==0)
                           : (land.gpu.enabled && (land.request&255)==1 && g_vk_present%150==0);
    if(geometryCapture.frame || geometryCapture.serial>=8 || !selected || !world.selected)return;
    if(!geometryCapture.mapped){
        size_t bytes=32ull*worldMaxVertices;
        if(!contact_buffer(&geometryCapture.buffer,bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true) ||
           vkMapMemory(g_vk.dev,geometryCapture.buffer.memory,0,bytes,0,(void**)&geometryCapture.mapped)){
            geometryCapture.serial=8;return;
        }
    }
    geometryCapture.frame=g_vk_present;geometryCapture.count=3u*(world.selected/3u);
    geometryCapture.draws=drawCount;geometryCapture.slots=textureCount+1;geometryCapture.serial++;
    memcpy(geometryCapture.axes,axes,48);memcpy(geometryCapture.origin,origin,16);
    memcpy(geometryCapture.materials,draws,drawCount*sizeof *draws);
    memset(geometryCapture.images,0,sizeof geometryCapture.images);
    for(u32 i=1;i<=textureCount;i++){
        WorldTexture *t=&textures[i];
        for(Surf *s=vk_surf_first(t->address);s;s=s->next){
            if(s->gva!=t->address || s->img!=t->image)continue;
            if(s->size<2048 || vk_compute_pending(s->gva,2048) || vk_buf_pending(s->gva,2048))break;
            const u8 *p=gmem_host(s->gva,2048);
            if(!p)break;
            u64 key=14695981039346656037ull;
            for(int j=0;j<2048;j++)key=(key^p[j])*1099511628211ull;
            geometryCapture.images[i].hash=key;geometryCapture.images[i].w=s->w;geometryCapture.images[i].h=s->h;
            break;
        }
    }
    VkDeviceSize len=16ull*geometryCapture.count;
    VkBufferCopy regions[]={{0,0,len},{16ull*worldMaxVertices,len,len}};
    contact_barrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    vkCmdCopyBuffer(cb,contact.vertices.handle,geometryCapture.buffer.handle,2,regions);
    contact_barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
}
