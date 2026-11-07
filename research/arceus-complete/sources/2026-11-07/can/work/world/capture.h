static struct {
    ContactBuffer buffer;
    float *positions;
    u32 frame, vertices, draws;
    int complete;
} worldCapture;
static int world_capture_begin(VkCommandBuffer cb,u32 vertices){
    enum { maxVertices=1500000 };
    if(worldCapture.complete || g_vk_present<450 || !vertices)return 0;
    if(!worldCapture.buffer.handle){
        if(!contact_buffer(&worldCapture.buffer,maxVertices*16,VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT,true) ||
           vkMapMemory(g_vk.dev,worldCapture.buffer.memory,0,maxVertices*16,0,(void**)&worldCapture.positions)){
            worldCapture.complete=1;
            return 0;
        }
        memset(worldCapture.positions,0,maxVertices*16);
        worldCapture.frame=g_vk_present;
    }
    if(worldCapture.frame!=g_vk_present){
        if(g_vk_present<=worldCapture.frame+3)return 0;
        vk_rec_sync();
        if(vkDeviceWaitIdle(g_vk.dev)!=VK_SUCCESS)return 0;
        const char *path=getenv("ARCEUS_WORLD_CAPTURE");
        FILE *file=path?fopen(path,"wb"):NULL;
        if(file){fwrite(worldCapture.positions,16,worldCapture.vertices,file);fclose(file);}
        LOG("world geometry capture: frame=%u draws=%u vertex slots=%u\n",worldCapture.frame,worldCapture.draws,worldCapture.vertices);
        worldCapture.complete=1;
        return 0;
    }
    if(vertices>maxVertices-worldCapture.vertices)return 0;
    VkDeviceSize offset=16ull*worldCapture.vertices, bytes=16ull*vertices;
    rec_CmdBindTransformFeedbackBuffersEXT(cb,0,1,&worldCapture.buffer.handle,&offset,&bytes);
    rec_CmdBeginTransformFeedbackEXT(cb,0,0,NULL,NULL);
    worldCapture.vertices+=vertices;worldCapture.draws++;
    return 1;
}
