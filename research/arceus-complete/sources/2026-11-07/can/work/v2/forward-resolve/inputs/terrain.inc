#define TERRAIN_SIDE 513u
#define TERRAIN_CELLS (TERRAIN_SIDE-1u)
#define TERRAIN_TRIS (2u*TERRAIN_CELLS*TERRAIN_CELLS)
#define TERRAIN_INFO_OFFSET (worldDrawLimit*(sizeof(WorldDrawMaterial)+worldTintBytes))
extern u64 arceus_land_request;
#include "props.inc"
typedef struct {
    ContactBuffer vertex;
    VkDeviceAddress index,colors;
    float materialGrid[4];
    u32 vertexCount,triangleCount,archiveBytes;
    bool attempted;
} LandArea;
typedef struct {u64 scene, vertices;u32 width,enabled;float jitter[2];PropInfo props;VkDeviceAddress index;u32 triangles,pad;VkDeviceAddress colors;u32 colorSide,pad2;float grid[4];} LandInfo;
_Static_assert(offsetof(PropInfo,properties)==48 && sizeof(PropInfo)==64,"retained material buffer layout");
_Static_assert(offsetof(LandInfo,props)==32 && offsetof(LandInfo,index)==96 && offsetof(LandInfo,grid)==128 && sizeof(LandInfo)==144,"terrain draw table layout");
static struct {
    LandArea area[12];
    ContactBuffer indices,instance;
    ContactTree bottom,top;
    int initialized,failed;
    u64 request;
    LandInfo gpu;
} land;
typedef struct {float xyz[3];u32 color;} LandVertex;
typedef struct {char magic[4];u32 version,tiles,sourceSize,a,b;} LandHeader;

static bool land_regular_indices(void){
    if(land.indices.address)return true;
    const VkDeviceSize bytes=TERRAIN_TRIS*3ull*sizeof(u32);
    VkBufferUsageFlags usage=VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if(!contact_buffer(&land.indices,bytes,usage,true))return false;
    u32 *idx;
    if(vkMapMemory(g_vk.dev,land.indices.memory,0,bytes,0,(void**)&idx)!=VK_SUCCESS){
        vkDestroyBuffer(g_vk.dev,land.indices.handle,NULL);
        vkFreeMemory(g_vk.dev,land.indices.memory,NULL);
        land.indices=(ContactBuffer){0};
        return false;
    }
    for(u32 z=0;z<TERRAIN_CELLS;z++)for(u32 x=0;x<TERRAIN_CELLS;x++){
        u32 a=z*TERRAIN_SIDE+x,b=a+1,c=a+TERRAIN_SIDE,d=c+1;
        *idx++=a;*idx++=c;*idx++=b;*idx++=b;*idx++=c;*idx++=d;
    }
    vkUnmapMemory(g_vk.dev,land.indices.memory);
    return true;
}

static bool land_regular_data(FILE *file,LandVertex *vertex){
    float grid[4];
    if(fread(grid,sizeof grid,1,file)!=1 || !isfinite(grid[0]) || !isfinite(grid[1]) ||
       !(grid[2]>0 && grid[2]<=32 && grid[3]>0 && grid[3]<=32))return false;
    for(u32 z=0;z<TERRAIN_SIDE;z++)for(u32 x=0;x<TERRAIN_SIDE;x++){
        LandVertex *v=vertex+z*TERRAIN_SIDE+x;
        if(fread(v->xyz+1,sizeof(float),1,file)!=1)return false;
        v->xyz[0]=grid[0]+x*grid[2];v->xyz[2]=grid[1]+z*grid[3];
    }
    for(u32 i=0;i<TERRAIN_SIDE*TERRAIN_SIDE;i++)
        if(fread(&vertex[i].color,sizeof(u32),1,file)!=1)return false;
    return true;
}

static bool land_read(FILE *file,LandArea *area,u32 archiveSize){
    LandHeader h;
    if(fread(&h,sizeof h,1,file)!=1 || memcmp(h.magic,"LAND",4) || h.tiles!=1 || h.sourceSize!=archiveSize)return false;
    const bool regular=h.version==2, texelColor=h.version==4;
    if(regular){
        if(h.a!=TERRAIN_SIDE || h.b!=TERRAIN_SIDE || !land_regular_indices())return false;
    }else if((h.version!=3 && !texelColor) || h.a<3 || h.a>TERRAIN_TRIS+2 || h.b<1 || h.b>TERRAIN_TRIS)return false;
    u32 nv=regular?TERRAIN_SIDE*TERRAIN_SIDE:h.a,nt=regular?TERRAIN_TRIS:h.b;
    size_t vertexBytes=nv*sizeof(LandVertex),indexWords=regular?0:3u*nt;
    float grid[4]={0};
    if(texelColor && (fread(grid,sizeof grid,1,file)!=1 || !isfinite(grid[0]) || !isfinite(grid[1]) ||
       !(grid[2]>0 && grid[2]<=32 && grid[3]>0 && grid[3]<=32)))return false;
    size_t colorWords=texelColor?TERRAIN_SIDE*TERRAIN_SIDE:0;
    size_t bytes=vertexBytes+(indexWords+colorWords)*sizeof(u32);
    LandVertex *data=malloc(bytes);
    if(!data)return false;
    u32 *indices=(u32*)(data+nv);
    bool ok=regular?land_regular_data(file,data):
        fread(data,sizeof(LandVertex),nv,file)==nv && fread(indices,sizeof(u32),indexWords,file)==indexWords;
    if(ok && colorWords)ok=fread(indices+indexWords,sizeof(u32),colorWords,file)==colorWords;
    ok=ok && fgetc(file)==EOF;
    for(u32 i=0;ok && i<nv;i++)
        ok=isfinite(data[i].xyz[0]) && isfinite(data[i].xyz[1]) && isfinite(data[i].xyz[2]);
    for(size_t i=0;ok && i<indexWords;i++)ok=indices[i]<nv;
    ContactBuffer buffer={0};
    VkBufferUsageFlags usage=VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if(ok)ok=contact_buffer(&buffer,bytes,usage,true);
    void *mapped=NULL;
    if(ok)ok=vkMapMemory(g_vk.dev,buffer.memory,0,bytes,0,&mapped)==VK_SUCCESS;
    if(ok){
        memcpy(mapped,data,bytes);vkUnmapMemory(g_vk.dev,buffer.memory);
        area->vertex=buffer;area->index=regular?land.indices.address:buffer.address+vertexBytes;
        if(texelColor){
            area->colors=area->index+indexWords*sizeof(u32);
            grid[2]=1.f/grid[2];grid[3]=1.f/grid[3];
            memcpy(area->materialGrid,grid,sizeof grid);
        }
        area->vertexCount=nv;area->triangleCount=nt;area->archiveBytes=archiveSize;
    }else if(buffer.handle){
        vkDestroyBuffer(g_vk.dev,buffer.handle,NULL);vkFreeMemory(g_vk.dev,buffer.memory,NULL);
    }
    free(data);
    return ok;
}

static bool land_load(u32 id,u32 archiveSize){
    LandArea *area=&land.area[id];
    if(area->attempted)return area->vertex.address && area->archiveBytes==archiveSize;
    area->attempted=true;
    const char *dir=getenv("ARCEUS_TERRAIN_DIR");
    if(!dir)return false;
    char path[1024];
    int length=snprintf(path,sizeof path,"%s/ha_area%02u.land",dir,id);
    if(length<0 || (size_t)length>=sizeof path)return false;
    FILE *file=fopen(path,"rb");
    if(!file)return false;
    bool ok=land_read(file,area,archiveSize);
    fclose(file);
    return ok;
}
static bool land_init(LandArea *area){
    VkBufferUsageFlags input=VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    land.bottom.geometry=(VkAccelerationStructureGeometryKHR){.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR,.flags=VK_GEOMETRY_OPAQUE_BIT_KHR,
        .geometry.triangles={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
          .vertexFormat=VK_FORMAT_R32G32B32_SFLOAT,.vertexData.deviceAddress=area->vertex.address,.vertexStride=16,
          .maxVertex=TERRAIN_TRIS+1,.indexType=VK_INDEX_TYPE_UINT32,.indexData.deviceAddress=area->index}};
    if(!contact_tree(&land.bottom,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,TERRAIN_TRIS))return false;
    if(!contact_buffer(&land.instance,2*sizeof(VkAccelerationStructureInstanceKHR),input|VK_BUFFER_USAGE_TRANSFER_DST_BIT,false))return false;
    land.top.geometry=(VkAccelerationStructureGeometryKHR){.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR,.geometry.instances={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .data.deviceAddress=land.instance.address}};
    return contact_tree(&land.top,VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,2);
}
static void land_update(VkCommandBuffer cb){
    u64 request=__atomic_load_n(&arceus_land_request,__ATOMIC_ACQUIRE);
    if(request==land.request)return;
    land.request=request;land.gpu=(LandInfo){0};
    if(!request || land.failed)return;
    u32 id=(u32)(request&255)-1;
    if(id>=12 || !land_load(id,(u32)(request>>8)))return;
    LandArea *area=&land.area[id];
    if(!land.initialized){
        if(!land_init(area)){land.failed=1;return;}
        land.initialized=1;
    }
    VkPipelineStageFlags build=VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    VkAccessFlags read=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,write=VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    land.bottom.geometry.geometry.triangles.vertexData.deviceAddress=area->vertex.address;
    land.bottom.geometry.geometry.triangles.indexData.deviceAddress=area->index;
    land.bottom.geometry.geometry.triangles.maxVertex=area->vertexCount-1;
    land.bottom.range.primitiveCount=area->triangleCount;
    bool haveProps=id==0 && props_load((u32)(request>>8));
    VkAccelerationStructureInstanceKHR instances[2]={{.transform.matrix={{1,0,0,0},{0,1,0,0},{0,0,1,0}},.mask=255,
        .flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,.accelerationStructureReference=contact_address(&land.bottom)}};
    land.top.range.primitiveCount=haveProps?2:1;
    contact_barrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    if(haveProps){
        instances[1]=instances[0];instances[1].instanceCustomIndex=1;instances[1].accelerationStructureReference=contact_address(&props.bottom);
        vkCmdFillBuffer(cb,props.data.handle,props.activeOffset,props.info.groups*4ull,0);
    }
    vkCmdUpdateBuffer(cb,land.instance.handle,0,land.top.range.primitiveCount*sizeof *instances,instances);
    contact_barrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT,build,read|write);
    contact_build(cb,&land.bottom);
    if(haveProps)contact_build(cb,&props.bottom);
    contact_barrier(cb,build,write,build,read);
    contact_build(cb,&land.top);
    contact_barrier(cb,build,write,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,read);
    land.gpu=(LandInfo){.scene=contact_address(&land.top),.vertices=area->vertex.address,.index=area->index,.triangles=area->triangleCount,.colors=area->colors,.colorSide=TERRAIN_SIDE,.enabled=1};
    memcpy(land.gpu.grid,area->materialGrid,sizeof land.gpu.grid);
    if(haveProps){land.gpu.props=props.info;LOG("world-pt: village prop geometry loaded, %u triangles in %u groups\n",props.info.triangles,props.info.groups);}
    LOG("world-pt: loaded terrain ha_area%02u, %u triangles independent of camera visibility\n",id,area->triangleCount);
}


