#ifndef ARCEUS_REPLAY_IO_H
#define ARCEUS_REPLAY_IO_H
#define CHECK(call) do {VkResult res=(call); if(res!=VK_SUCCESS){fprintf(stderr,"%s failed (%d)\n",#call,res);exit(2);}} while(0)
typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    unsigned char *bytes;
} HostBuffer;

static HostBuffer host_buffer(VkPhysicalDevice physical,VkDevice device,VkDeviceSize size,VkBufferUsageFlags usage) {
    HostBuffer out={0};
    VkBufferCreateInfo create={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};
    CHECK(vkCreateBuffer(device,&create,NULL,&out.buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device,out.buffer,&req);
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(physical,&mem);
    uint32_t type=0;
    VkMemoryPropertyFlags need=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while(type<mem.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type)) ||
          (mem.memoryTypes[type].propertyFlags&need)!=need)) ++type;
    if(type==mem.memoryTypeCount) exit(4);
    VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
    CHECK(vkAllocateMemory(device,&alloc,NULL,&out.memory));CHECK(vkBindBufferMemory(device,out.buffer,out.memory,0));
    CHECK(vkMapMemory(device,out.memory,0,size,0,(void**)&out.bytes));
    return out;
}
static void destroy_buffer(VkDevice device,HostBuffer b) {
    vkUnmapMemory(device,b.memory);vkDestroyBuffer(device,b.buffer,NULL);vkFreeMemory(device,b.memory,NULL);
}
static void image_file(const char *stem,const char *suffix,unsigned char *data,size_t size,int writeFile) {
    char path[512];snprintf(path,sizeof path,"%s.%s",stem,suffix);
    FILE *f=fopen(path,writeFile?"wb":"rb");
    if(!f || (writeFile?fwrite(data,1,size,f):fread(data,1,size,f))!=size){fprintf(stderr,"I/O failed: %s\n",path);exit(3);}
    fclose(f);
}
#endif
