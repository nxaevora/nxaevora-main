#include "contact_words.h"

extern int arceus_checkpoints;
static void world_mark(VkCommandBuffer cb,uintptr_t marker){
    if(arceus_checkpoints)vkCmdSetCheckpointNV(cb,(void*)(((uintptr_t)g_vk_present<<8)|marker));
}
enum { rtTriangles = 500000 };
typedef struct {
    VkBuffer handle;
    VkDeviceMemory memory;
    VkDeviceAddress address;
} ContactBuffer;
typedef struct {
    VkAccelerationStructureKHR handle;
    ContactBuffer storage, scratch;
    union {VkAccelerationStructureGeometryKHR geometry,geometries[2];};
    VkAccelerationStructureBuildGeometryInfoKHR build;
    union {VkAccelerationStructureBuildRangeInfoKHR range,ranges[2];};
} ContactTree;
#include "rr-frame.h"
static struct {
    ContactBuffer buffer;
    VkPipeline pipeline;
    THist images[RR_GUIDE_COUNT];
    RrGuides guides;
    bool attempted,ready,writing;
} rr;
static struct {
    int attempted, ready;
    VkPipeline meshPipeline, shadePipeline,verifyPipeline,calibrationPipeline;
    ContactBuffer vertices, instance, counters, primaryLight;
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
    if(type==props.memoryTypeCount)goto failed;
    VkMemoryAllocateFlagsInfo flags={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&flags,.allocationSize=req.size,.memoryTypeIndex=type};
    if(vkAllocateMemory(g_vk.dev,&alloc,NULL,&b->memory) || vkBindBufferMemory(g_vk.dev,b->handle,b->memory,0))goto failed;
    VkBufferDeviceAddressInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer=b->handle};
    b->address=vkGetBufferDeviceAddress(g_vk.dev,&info);
    if(b->address)return true;
failed:
    vkDestroyBuffer(g_vk.dev,b->handle,NULL);
    if(b->memory)vkFreeMemory(g_vk.dev,b->memory,NULL);
    *b=(ContactBuffer){0};
    return false;
}

static bool contact_tree_parts(ContactTree *tree,VkAccelerationStructureTypeKHR type,u32 count,u32 parts){
    tree->range.primitiveCount=count;
    tree->build=(VkAccelerationStructureBuildGeometryInfoKHR){.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type=type,.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,.geometryCount=parts,.pGeometries=tree->geometries};
    VkAccelerationStructureBuildSizesInfoKHR sizes={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    u32 limits[2]={count,count};
    vkGetAccelerationStructureBuildSizesKHR(g_vk.dev,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&tree->build,limits,&sizes);
    if(!contact_buffer(&tree->storage,sizes.accelerationStructureSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,false) ||
       !contact_buffer(&tree->scratch,sizes.buildScratchSize+contact.alignment,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false))return false;
    VkAccelerationStructureCreateInfoKHR ci={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer=tree->storage.handle,.size=sizes.accelerationStructureSize,.type=type};
    if(vkCreateAccelerationStructureKHR(g_vk.dev,&ci,NULL,&tree->handle))return false;
    tree->build.dstAccelerationStructure=tree->handle;
    tree->build.scratchData.deviceAddress=(tree->scratch.address+contact.alignment-1)&~((u64)contact.alignment-1);
    return true;
}
static bool contact_tree(ContactTree *tree,VkAccelerationStructureTypeKHR type,u32 count){
    return contact_tree_parts(tree,type,count,1);
}

static VkDeviceAddress contact_address(ContactTree *tree){
    VkAccelerationStructureDeviceAddressInfoKHR info={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure=tree->handle};
    return vkGetAccelerationStructureDeviceAddressKHR(g_vk.dev,&info);
}

static bool contact_pipeline(const u32 *words,size_t bytes,VkPipeline *pipe){
    VkShaderModule module=vk_module_spv(words,bytes/4);
    if(!module)return false;
    const VkSpecializationMapEntry entry={0,0,sizeof arceus_texture_slots};
    const VkSpecializationInfo specialization={1,&entry,sizeof arceus_texture_slots,&arceus_texture_slots};
    VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=tpl,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main",.pSpecializationInfo=&specialization}};
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
    if(!contact_buffer(&contact.vertices,rtTriangles*6*16,input|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,false))return false;
    contact.bottom.geometry=(VkAccelerationStructureGeometryKHR){.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR,.flags=VK_GEOMETRY_OPAQUE_BIT_KHR,
        .geometry.triangles={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData.deviceAddress=contact.vertices.address,.vertexStride=16,.maxVertex=rtTriangles*3-1,.indexType=VK_INDEX_TYPE_NONE_KHR}};
    contact.bottom.geometries[1]=contact.bottom.geometry;
    contact.bottom.geometries[1].flags=0;
    if(!contact_tree_parts(&contact.bottom,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,rtTriangles,2))return false;
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
    const char *filter=getenv("ARCEUS_PT_FILTER");
    if(!filter || strcmp(filter,"0"))
        contact_buffer(&contact.primaryLight,2ull*1920*1080*8,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false);
    if(getenv("ARCEUS_RT_VERIFY")){
        if(!contact_buffer(&contact.counters,65536,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true) ||
           vkMapMemory(g_vk.dev,contact.counters.memory,0,65536,0,(void**)&contact.mappedCounters))return false;
        memset(contact.mappedCounters,0,65536);
    }
    return contact.scene && contact_pipeline(verifyObjects_words,sizeof verifyObjects_words,&contact.verifyPipeline) && contact_pipeline(calibrate_words,sizeof calibrate_words,&contact.calibrationPipeline) && contact_pipeline(mesh_words,sizeof mesh_words,&contact.meshPipeline) &&
           contact_pipeline(contact_words,sizeof contact_words,&contact.shadePipeline);
}
static void contact_barrier(VkCommandBuffer cb,VkPipelineStageFlags src,VkAccessFlags writes,VkPipelineStageFlags dst,VkAccessFlags reads){
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=writes,.dstAccessMask=reads};
    vkCmdPipelineBarrier(cb,src,dst,0,1,&barrier,0,NULL,0,NULL);
}
static void contact_build(VkCommandBuffer cb,ContactTree *tree){
    const VkAccelerationStructureBuildRangeInfoKHR *range=tree->ranges;
    vkCmdBuildAccelerationStructuresKHR(cb,1,&tree->build,&range);
}
 #include "material.inc"
#include "terrain.inc"
#include "stream.inc"
#include "static-capture.inc"
static bool contact_record(VkCommandBuffer cb,VkDescriptorSet set,const float *camera,u32 surfaceInputs,const float jitter[2]){
    if(world.used<300 || !world.clip.address || world.frame!=g_vk_present)return false;
    if(!contact.attempted){
        contact.attempted=1;
        contact.ready=contact_init();
        LOG("arceus-rt: five-bounce path tracing initialization %s\n",contact.ready?"ready":"failed");
    }
    if(!contact.ready)return false;
    struct {float camera[4];u64 vertices,scene,stats;u32 frame,count;u64 clip;u64 draws;float axes[3][4],origin[4];} pc={
        .camera={sqrtf(camera[0]*camera[0]+camera[1]*camera[1]+camera[2]*camera[2]),
                 sqrtf(camera[4]*camera[4]+camera[5]*camera[5]+camera[6]*camera[6]),camera[11],camera[15]},
        .vertices=contact.vertices.address,.scene=contact.scene,.stats=contact.counters.address,.frame=g_vk_present,.count=world.used/3,.clip=world.clip.address,.draws=drawTable.address};
    float horizontal[3],vertical[3],forward[3];
    for(int i=0;i<3;i++){horizontal[i]=camera[i]/pc.camera[0];vertical[i]=-camera[i+4]/pc.camera[1];}
    for(int i=0;i<3;i++)forward[i]=horizontal[(i+1)%3]*vertical[(i+2)%3]-horizontal[(i+2)%3]*vertical[(i+1)%3];
    float len=0;
    for(int i=0;i<3;i++){
        forward[i]=camera[12+i]-(camera[8+i]/camera[11])*camera[15];len+=forward[i]*forward[i];
    }
    if(!(len>1.e-10f) || !isfinite(len))return false;
    for(int i=0;i<3;i++){pc.axes[0][i]=horizontal[i];pc.axes[1][i]=vertical[i];pc.axes[2][i]=forward[i]/sqrtf(len);}
    pc.axes[0][3]=(float)drawCount;
    pc.origin[3]=(float)surfaceInputs;
    u32 opaque=0,start=0;
    for(u32 j=0;j<drawCount;j++){
        u32 n=draws[j].end-start;start=draws[j].end;
        if(!(draws[j].slot>>8)){draws[j].base=opaque;opaque+=n;}
    }
    u32 transparent=opaque;start=0;
    for(u32 j=0;j<drawCount;j++){
        u32 n=draws[j].end-start;start=draws[j].end;
        if(draws[j].slot>>8){draws[j].base=transparent;transparent+=n;}
    }
    pc.axes[2][3]=(float)opaque;
    for(int i=0;i<3;i++)pc.origin[i]=camera[8+i]/camera[11];
    if(rr.writing){
        // Row-vector world-to-view with +Y up, +Z forward. The trace uses screen-down Y.
        for(unsigned axis=0;axis<3;axis++){
            float translation=0;
            for(unsigned j=0;j<3;j++){
                float basis=pc.axes[axis][j]*(axis==1?-1.f:1.f);
                rr.guides.worldToView[4*j+axis]=basis;
                translation-=pc.origin[j]*basis;
            }
            rr.guides.worldToView[12+axis]=translation;
        }
        rr.guides.worldToView[15]=1;
        rr.guides.viewToClip[0]=1.f/pc.camera[0]; rr.guides.viewToClip[5]=1.f/pc.camera[1];
        rr.guides.viewToClip[10]=-camera[15]/camera[11]; rr.guides.viewToClip[11]=1;
        rr.guides.viewToClip[14]=1.f/camera[11];
    }
    contact.bottom.ranges[0].primitiveCount=opaque;
    contact.bottom.ranges[1].primitiveCount=pc.count-opaque;
    contact.bottom.ranges[1].primitiveOffset=opaque*3u*16u;
    LOG_LIMIT(6,"world-pt: submitted %u triangles, %u over-cap draws\n",pc.count,world.skipped);
    if(contact.mappedCounters && getenv("ARCEUS_GEOMETRY_SAMPLES") && g_vk_present%120==0 && g_vk_present!=contact.diagnosticFrame){
        if(vkDeviceWaitIdle(g_vk.dev)==VK_SUCCESS){
            char filename[1024];
            snprintf(filename,sizeof filename,"%s-%05u.bin",getenv("ARCEUS_GEOMETRY_SAMPLES"),g_vk_present);
            FILE *samples=fopen(filename,"wb");
            if(samples){
                // Matrix is emitted by the same shader invocation group as the depth samples below.
                fwrite(contact.mappedCounters+32,1,65536-128,samples);
                fclose(samples);
            }
        }
    }
    pc.axes[1][3]=(getenv("ARCEUS_GEOMETRY_SAMPLES") && contact.mappedCounters)?1.f:0.f;
    pc.camera[2]=camera[11];pc.camera[3]=camera[15];
    if(contact.mappedCounters && contact.diagnosticFrame!=g_vk_present && g_vk_present%90==0){
        vkDeviceWaitIdle(g_vk.dev);
        LOG_LIMIT(100,"arceus-rt: sampled primary=%u primaryHits=%u matchingDepth=%u secondaryHits=%u texturedOffscreen=%u unresolved=%u textureSlots=%u drawColor=%u eval=%u grayError=%u rawError=%u fittedError=%u terrainPrimary=%u terrainSecondary=%u terrainShadow=%u glossyPaths=%u glossyHits=%u cutouts=%u rasterGeometry=%u missingPrimary=%u primaryMismatch=%u skippedDraws=%u directVisible=%u directOccluded=%u emitterHits=%u bounceEmitters=%u inputMask=%u recovered=%u retainedPrimary=%u retainedBounce=%u retainedShadow=%u deepHits=%u maxBounce=%u retainedPbrHits=%u\n",contact.mappedCounters[0],contact.mappedCounters[1],contact.mappedCounters[2],contact.mappedCounters[3],contact.mappedCounters[4],contact.mappedCounters[5],textureCount,contact.mappedCounters[6],contact.mappedCounters[7],contact.mappedCounters[8],contact.mappedCounters[9],contact.mappedCounters[10],contact.mappedCounters[11],contact.mappedCounters[12],contact.mappedCounters[13],contact.mappedCounters[14],contact.mappedCounters[15],contact.mappedCounters[16],contact.mappedCounters[17],contact.mappedCounters[18],contact.mappedCounters[19],world.skipped,contact.mappedCounters[20],contact.mappedCounters[21],contact.mappedCounters[22],contact.mappedCounters[23],surfaceInputs,contact.mappedCounters[24],contact.mappedCounters[25],contact.mappedCounters[26],contact.mappedCounters[27],contact.mappedCounters[28],contact.mappedCounters[29],contact.mappedCounters[30]);
        contact.diagnosticFrame=g_vk_present;
    }
    vk_rec_sync();
    VkPipelineStageFlags build=VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,compute=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkAccessFlags asWrite=VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,asRead=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    contact_barrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
                    build|compute,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT);
    contact_barrier(cb,compute,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    world_mark(cb,1);
    land_update(cb);
    memcpy(land.gpu.jitter,jitter,sizeof land.gpu.jitter);
    vkCmdUpdateBuffer(cb,drawTable.handle,TERRAIN_INFO_OFFSET,sizeof land.gpu,&land.gpu);
    vkCmdUpdateBuffer(cb,drawTable.handle,TERRAIN_INFO_OFFSET+sizeof land.gpu,sizeof mainLight,&mainLight);
    vkCmdUpdateBuffer(cb,drawTable.handle,TERRAIN_INFO_OFFSET+sizeof land.gpu+sizeof mainLight,
        sizeof contact.primaryLight.address,&contact.primaryLight.address);
    if(rr.writing)vkCmdUpdateBuffer(cb,drawTable.handle,TERRAIN_INFO_OFFSET+sizeof land.gpu+sizeof mainLight+sizeof(VkDeviceAddress),
        sizeof rr.buffer.address,&rr.buffer.address);
    vkCmdFillBuffer(cb,drawTable.handle,worldDrawLimit*sizeof(WorldDrawMaterial),worldDrawLimit*worldTintBytes,0);
    for(u32 offset=0;offset<drawCount;){
        u32 n=drawCount-offset;
        if(n>65536/sizeof *draws)n=65536/sizeof *draws;
        vkCmdUpdateBuffer(cb,drawTable.handle,offset*sizeof *draws,n*sizeof *draws,draws+offset);
        offset+=n;
    }
    contact_barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,compute,VK_ACCESS_SHADER_READ_BIT);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tpl,0,1,&set,0,NULL);
    vkCmdPushConstants(cb,tpl,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof pc,&pc);
    world_mark(cb,2);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.meshPipeline);
    vkCmdDispatch(cb,(pc.count+63)/64,1,1);
    capture_geometry(cb,pc.axes,pc.origin);
    if(land.gpu.props.groups){
        contact_barrier(cb,compute,VK_ACCESS_SHADER_WRITE_BIT,compute,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.verifyPipeline);
        vkCmdDispatch(cb,(land.gpu.props.groups+63)/64,1,1);
        contact_barrier(cb,compute,VK_ACCESS_SHADER_WRITE_BIT,compute,VK_ACCESS_SHADER_READ_BIT);
    }
    contact_barrier(cb,compute,VK_ACCESS_SHADER_WRITE_BIT,build,VK_ACCESS_SHADER_READ_BIT);
    world_mark(cb,3);
    contact_build(cb,&contact.bottom);
    contact_barrier(cb,build,asWrite,build,asRead);
    contact_build(cb,&contact.top);
    contact_barrier(cb,build,asWrite,compute,asRead);
    world_mark(cb,4);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.calibrationPipeline);
    vkCmdDispatch(cb,30,17,1);
    contact_barrier(cb,compute,VK_ACCESS_SHADER_WRITE_BIT,compute,VK_ACCESS_SHADER_READ_BIT);
    world_mark(cb,5);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,rr.writing?rr.pipeline:contact.shadePipeline);
    vkCmdDispatch(cb,240,135,1);
    if(contact.mappedCounters)contact_barrier(cb,compute,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
    world_mark(cb,6);
    vk_render_forget_state();
    return true;
}









