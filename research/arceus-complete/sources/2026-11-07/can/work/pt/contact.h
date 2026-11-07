#include "contact_words.h"

enum { rtTriangles = 240*135*2 };
typedef struct {
    VkBuffer handle;
    VkDeviceMemory memory;
    VkDeviceAddress address;
} ContactBuffer;
typedef struct {
    VkAccelerationStructureKHR handle;
    ContactBuffer storage, scratch;
    VkAccelerationStructureGeometryKHR geometry;
    VkAccelerationStructureBuildGeometryInfoKHR build;
    VkAccelerationStructureBuildRangeInfoKHR range;
} ContactTree;
static struct {
    int attempted, ready;
    VkPipeline meshPipeline, shadePipeline;
    ContactBuffer vertices, instance, counters;
    ContactTree bottom, top;
    VkDeviceAddress scene;
    u32 alignment, diagnosticFrame;
    u32 *mappedCounters;
} contact;
extern int arceus_contact_supported;

static bool contact_buffer(ContactBuffer *b,VkDeviceSize bytes,VkBufferUsageFlags usage,bool host){
    VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,
        .usage=usage|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    if(vkCreateBuffer(g_vk.dev,&ci,NULL,&b->handle))return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_vk.dev,b->handle,&req);
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(g_vk.phys,&props);
    VkMemoryPropertyFlags need=host?(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT):VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    u32 type=0;
    while(type<props.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type)) ||
          (props.memoryTypes[type].propertyFlags&need)!=need))++type;
    if(type==props.memoryTypeCount)return false;
    VkMemoryAllocateFlagsInfo flags={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&flags,.allocationSize=req.size,.memoryTypeIndex=type};
    if(vkAllocateMemory(g_vk.dev,&alloc,NULL,&b->memory) || vkBindBufferMemory(g_vk.dev,b->handle,b->memory,0))return false;
    VkBufferDeviceAddressInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer=b->handle};
    b->address=vkGetBufferDeviceAddress(g_vk.dev,&info);
    return b->address!=0;
}

static bool contact_tree(ContactTree *tree,VkAccelerationStructureTypeKHR type,u32 count){
    tree->range.primitiveCount=count;
    tree->build=(VkAccelerationStructureBuildGeometryInfoKHR){.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type=type,.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,.geometryCount=1,.pGeometries=&tree->geometry};
    VkAccelerationStructureBuildSizesInfoKHR sizes={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(g_vk.dev,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&tree->build,&count,&sizes);
    if(!contact_buffer(&tree->storage,sizes.accelerationStructureSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,false) ||
       !contact_buffer(&tree->scratch,sizes.buildScratchSize+contact.alignment,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false))return false;
    VkAccelerationStructureCreateInfoKHR ci={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer=tree->storage.handle,.size=sizes.accelerationStructureSize,.type=type};
    if(vkCreateAccelerationStructureKHR(g_vk.dev,&ci,NULL,&tree->handle))return false;
    tree->build.dstAccelerationStructure=tree->handle;
    tree->build.scratchData.deviceAddress=(tree->scratch.address+contact.alignment-1)&~((u64)contact.alignment-1);
    return true;
}

static VkDeviceAddress contact_address(ContactTree *tree){
    VkAccelerationStructureDeviceAddressInfoKHR info={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure=tree->handle};
    return vkGetAccelerationStructureDeviceAddressKHR(g_vk.dev,&info);
}

static bool contact_pipeline(const u32 *words,size_t bytes,VkPipeline *pipe){
    VkShaderModule module=vk_module_spv(words,bytes/4);
    if(!module)return false;
    VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=tpl,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"}};
    VkResult result=vkCreateComputePipelines(g_vk.dev,vk_pipeline_cache(),1,&ci,NULL,pipe);
    vkDestroyShaderModule(g_vk.dev,module,NULL);
    return result==VK_SUCCESS;
}
static bool contact_init(void){
    VkPhysicalDeviceAccelerationStructurePropertiesKHR rt={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&rt};
    vkGetPhysicalDeviceProperties2(g_vk.phys,&props);
    contact.alignment=rt.minAccelerationStructureScratchOffsetAlignment;
    VkBufferUsageFlags input=VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if(!contact_buffer(&contact.vertices,rtTriangles*3*16,input|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false))return false;
    contact.bottom.geometry=(VkAccelerationStructureGeometryKHR){.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR,.flags=VK_GEOMETRY_OPAQUE_BIT_KHR,
        .geometry.triangles={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData.deviceAddress=contact.vertices.address,.vertexStride=16,.maxVertex=rtTriangles*3-1,.indexType=VK_INDEX_TYPE_NONE_KHR}};
    if(!contact_tree(&contact.bottom,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,rtTriangles))return false;
    VkAccelerationStructureInstanceKHR instance={.transform.matrix={{1,0,0,0},{0,1,0,0},{0,0,1,0}},.mask=255,
        .flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,.accelerationStructureReference=contact_address(&contact.bottom)};
    if(!contact_buffer(&contact.instance,sizeof instance,input,true))return false;
    void *mapped=NULL;
    if(vkMapMemory(g_vk.dev,contact.instance.memory,0,sizeof instance,0,&mapped))return false;
    memcpy(mapped,&instance,sizeof instance);
    vkUnmapMemory(g_vk.dev,contact.instance.memory);
    contact.top.geometry=(VkAccelerationStructureGeometryKHR){.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR,.geometry.instances={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .data.deviceAddress=contact.instance.address}};
    if(!contact_tree(&contact.top,VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,1))return false;
    contact.scene=contact_address(&contact.top);
    if(getenv("ARCEUS_RT_VERIFY")){
        if(!contact_buffer(&contact.counters,16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true) ||
           vkMapMemory(g_vk.dev,contact.counters.memory,0,16,0,(void**)&contact.mappedCounters))return false;
        memset(contact.mappedCounters,0,16);
    }
    return contact.scene && contact_pipeline(mesh_words,sizeof mesh_words,&contact.meshPipeline) &&
           contact_pipeline(contact_words,sizeof contact_words,&contact.shadePipeline);
}
static void contact_barrier(VkCommandBuffer cb,VkPipelineStageFlags src,VkAccessFlags writes,VkPipelineStageFlags dst,VkAccessFlags reads){
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=writes,.dstAccessMask=reads};
    vkCmdPipelineBarrier(cb,src,dst,0,1,&barrier,0,NULL,0,NULL);
}
static void contact_build(VkCommandBuffer cb,ContactTree *tree){
    const VkAccelerationStructureBuildRangeInfoKHR *range=&tree->range;
    vkCmdBuildAccelerationStructuresKHR(cb,1,&tree->build,&range);
}
static void contact_record(VkCommandBuffer cb,VkDescriptorSet set,const float *camera){
    if(!arceus_contact_supported)return;
    if(!contact.attempted){
        contact.attempted=1;
        contact.ready=contact_init();
        LOG("arceus-rt: two-bounce diffuse path tracing initialization %s\n",contact.ready?"ready":"failed");
    }
    if(!contact.ready)return;
    struct {float camera[4];u64 vertices,scene,stats;u32 frame;} pc={
        .camera={sqrtf(camera[0]*camera[0]+camera[1]*camera[1]+camera[2]*camera[2]),
                 sqrtf(camera[4]*camera[4]+camera[5]*camera[5]+camera[6]*camera[6]),camera[11],camera[15]},
        .vertices=contact.vertices.address,.scene=contact.scene,.stats=contact.counters.address,.frame=g_vk_present};
    if(contact.mappedCounters && contact.diagnosticFrame!=g_vk_present && g_vk_present%30==0){
        vkDeviceWaitIdle(g_vk.dev);
        LOG_LIMIT(16,"arceus-rt: sampled AO rays=%u hits=%u secondBounceHits=%u litSecondBounces=%u\n",contact.mappedCounters[0],contact.mappedCounters[1],contact.mappedCounters[2],contact.mappedCounters[3]);
        contact.diagnosticFrame=g_vk_present;
    }
    vk_rec_sync();
    VkPipelineStageFlags build=VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,compute=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkAccessFlags asWrite=VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,asRead=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    contact_barrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
                    build|compute,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tpl,0,1,&set,0,NULL);
    vkCmdPushConstants(cb,tpl,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof pc,&pc);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.meshPipeline);
    vkCmdDispatch(cb,30,17,1);
    contact_barrier(cb,compute,VK_ACCESS_SHADER_WRITE_BIT,build,VK_ACCESS_SHADER_READ_BIT);
    contact_build(cb,&contact.bottom);
    contact_barrier(cb,build,asWrite,build,asRead);
    contact_build(cb,&contact.top);
    contact_barrier(cb,build,asWrite,compute,asRead);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.shadePipeline);
    vkCmdDispatch(cb,240,135,1);
    if(contact.mappedCounters)contact_barrier(cb,compute,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
    vk_render_forget_state();
}
