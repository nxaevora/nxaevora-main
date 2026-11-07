enum { referenceWidth=480,referenceHeight=270,referenceSamples=128 };
typedef struct {float camera[4];u64 vertices,scene,stats;u32 frame,count;u64 clip,draws;float axes[3][4],origin[4];} WorldPush;
static struct {ContactBuffer buffer;VkPipeline pipeline;float *mapped;u32 frame;bool saved;} reference;
static void reference_capture(VkCommandBuffer cb,const WorldPush *pc){
    const char *path=getenv("ARCEUS_QA_REFERENCE");
    if(!path || reference.saved)return;
    size_t bytes=3ull*referenceWidth*referenceHeight*16;
    if(reference.frame && g_vk_present>reference.frame+2){
        contact_barrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
        contact_wait_readback();
        FILE *f=fopen(path,"wb");
        if(f){fwrite(reference.mapped,1,bytes,f);fclose(f);}
        reference.saved=true;return;
    }
    if(reference.frame || rr.writing || g_vk_present!=rr_audit_frame())return;
    reference.frame=g_vk_present;
    if(!contact_buffer(&reference.buffer,bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true) ||
       vkMapMemory(g_vk.dev,reference.buffer.memory,0,bytes,0,(void**)&reference.mapped)!=VK_SUCCESS ||
       !contact_pipeline(reference_words,sizeof reference_words,&reference.pipeline)){reference.saved=true;return;}
    memset(reference.mapped,0,bytes);
    WorldPush test=*pc;
    test.clip=reference.buffer.address;test.stats=0;
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,reference.pipeline);
    for(u32 i=0;i<referenceSamples;i++){
        test.axes[1][3]=-(float)(i+1);
        vkCmdPushConstants(cb,tpl,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof test,&test);
        vkCmdDispatch(cb,(referenceWidth+7)/8,(referenceHeight+7)/8,1);
        contact_barrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    }
    vkCmdPushConstants(cb,tpl,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof *pc,pc);
    LOG("world-pt: frozen reference at present %u with %u samples per pixel\n",reference.frame,referenceSamples);
}
