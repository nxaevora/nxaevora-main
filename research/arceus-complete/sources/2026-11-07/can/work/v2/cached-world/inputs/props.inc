typedef struct {
    u64 vertices,tags,atlas,checks,active;
    u32 triangles,groups;
    u64 properties;u32 firstPlaced,classCount;
} PropInfo;
typedef struct {ContactBuffer data;ContactTree bottom;PropInfo info;u64 activeOffset;bool attempted,built;} PropArea;
static PropArea propAreas[13];
static bool props_load(PropArea *props,u32 area,u32 archiveSize){
    if(props->attempted)return props->info.triangles!=0;
    props->attempted=1;
    const char *dir=getenv("ARCEUS_TERRAIN_DIR");
    char path[1024];
    if(!dir || snprintf(path,sizeof path,area==12?"%s/ha_area00_s01.props":"%s/ha_area%02u.props",dir,area)>=(int)sizeof path)return false;
    FILE *f=fopen(path,"rb");
    if(!f)return false;
    struct {char magic[4];u32 version,triangles,materials,groups,archive;} h;
    bool ok=fread(&h,sizeof h,1,f)==1 && !memcmp(h.magic,"PROP",4) && (h.version>=1 && h.version<=4) &&
        h.archive==archiveSize && h.triangles>0 && h.triangles<=1000000 &&
        h.materials>0 && h.materials<=255 && h.groups>0 && h.groups<=1024;
    u32 firstPlaced=h.groups;
    if(ok && h.version>=3)ok=fread(&firstPlaced,sizeof firstPlaced,1,f)==1 && firstPlaced<=h.groups;
    if(!ok){fclose(f);return false;}
    u32 classes=h.version==4?h.materials:0;
    u64 tags=48ull*h.triangles,atlas=tags+4ull*h.triangles;
    u64 properties=atlas+16384ull*h.materials;
    u64 payload=properties+(h.version>=2?16384ull*h.materials:0);
    u64 check=(payload+15)&~15ull,active=check+64ull*h.groups;
    VkBufferUsageFlags usage=VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    void *mapped=NULL;
    ok=contact_buffer(&props->data,active+4ull*(h.groups+classes),usage,true) &&
       vkMapMemory(g_vk.dev,props->data.memory,0,VK_WHOLE_SIZE,0,&mapped)==VK_SUCCESS;
    if(ok){
        u8 *data=mapped;
        ok=fread(data,1,payload,f)==payload &&
            fread(data+check,64,h.groups,f)==h.groups && fgetc(f)==EOF;
        for(u32 i=0;ok && i<h.triangles;i++){
            u32 tag=((u32*)(data+tags))[i];
            if((tag&255)>=h.materials || (tag>>16)>=h.groups)ok=false;
            else if(classes && ((const float*)(data+check+64*(tag>>16)))[7]!=(float)((tag&255)+1))ok=false;
            for(u32 j=0;j<3;j++){
                const float *p=(float*)data+12*i+4*j;
                if(!isfinite(p[0])||!isfinite(p[1])||!isfinite(p[2]))ok=false;
            }
        }
        for(u32 i=0;ok && i<h.groups;i++){
            const u32 *key=(const u32*)(data+check+64*i);
            const float witnessStart=((const float*)key)[11];
            if(!isfinite(witnessStart) || witnessStart<0 || witnessStart>3*h.triangles ||
               floorf(witnessStart)!=witnessStart || (witnessStart>0 && witnessStart-1+3*key[0]>3*h.triangles))ok=false;
            if(!key[0] || key[0]>h.triangles || key[1]>=3*key[0] || key[2]>=3*key[0] || key[3]>=3*key[0])ok=false;
        }
        if(classes){
            for(u32 i=0;ok && i<h.groups;i++){
                float model=((const float*)(data+check+64*i))[7];
                if(!(model>=1.f && model<=classes && floorf(model)==model))ok=false;
            }
        }
        memset(data+active,0,4ull*(h.groups+classes));
        vkUnmapMemory(g_vk.dev,props->data.memory);
    }
    fclose(f);
    if(!ok){
        if(props->data.handle)vkDestroyBuffer(g_vk.dev,props->data.handle,NULL);
        if(props->data.memory)vkFreeMemory(g_vk.dev,props->data.memory,NULL);
        props->data=(ContactBuffer){0};return false;
    }
    props->bottom.geometry=(VkAccelerationStructureGeometryKHR){.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry.triangles={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat=VK_FORMAT_R32G32B32_SFLOAT,.vertexData.deviceAddress=props->data.address,.vertexStride=16,
            .maxVertex=3*h.triangles-1,.indexType=VK_INDEX_TYPE_NONE_KHR}};
    if(!contact_tree(&props->bottom,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,h.triangles))return false;
    u64 address=props->data.address;
    props->info=(PropInfo){.vertices=address,.tags=address+tags,.atlas=address+atlas,.checks=address+check,
        .active=address+active,.triangles=h.triangles,.groups=h.groups,.properties=h.version>=2?address+properties:0,.firstPlaced=firstPlaced,.classCount=classes};
    props->activeOffset=active;
    return true;
}
