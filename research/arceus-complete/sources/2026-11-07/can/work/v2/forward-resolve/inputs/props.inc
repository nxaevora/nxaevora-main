typedef struct {
    u64 vertices,tags,atlas,checks,active;
    u32 triangles,groups;
    u64 properties,padding;
} PropInfo;
static struct {ContactBuffer data;ContactTree bottom;PropInfo info;u64 activeOffset;int attempted;} props;
static bool props_load(u32 archiveSize){
    if(props.attempted)return props.info.triangles!=0;
    props.attempted=1;
    const char *dir=getenv("ARCEUS_TERRAIN_DIR");
    char path[1024];
    if(!dir || snprintf(path,sizeof path,"%s/ha_area00.props",dir)>=(int)sizeof path)return false;
    FILE *f=fopen(path,"rb");
    if(!f)return false;
    struct {char magic[4];u32 version,triangles,materials,groups,archive;} h;
    bool ok=fread(&h,sizeof h,1,f)==1 && !memcmp(h.magic,"PROP",4) && (h.version==1 || h.version==2) &&
        h.archive==archiveSize && h.triangles>0 && h.triangles<=1000000 &&
        h.materials>0 && h.materials<=255 && h.groups>0 && h.groups<=1024;
    if(!ok){fclose(f);return false;}
    u64 tags=48ull*h.triangles,atlas=tags+4ull*h.triangles;
    u64 properties=atlas+16384ull*h.materials;
    u64 payload=properties+(h.version==2?16384ull*h.materials:0);
    u64 check=(payload+15)&~15ull,active=check+64ull*h.groups;
    VkBufferUsageFlags usage=VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    void *mapped=NULL;
    ok=contact_buffer(&props.data,active+4ull*h.groups,usage,true) &&
       vkMapMemory(g_vk.dev,props.data.memory,0,VK_WHOLE_SIZE,0,&mapped)==VK_SUCCESS;
    if(ok){
        u8 *data=mapped;
        ok=fread(data,1,payload,f)==payload &&
            fread(data+check,64,h.groups,f)==h.groups && fgetc(f)==EOF;
        for(u32 i=0;ok && i<h.triangles;i++){
            u32 tag=((u32*)(data+tags))[i];
            if((tag&255)>=h.materials || (tag>>16)>=h.groups)ok=false;
            for(u32 j=0;j<3;j++){
                const float *p=(float*)data+12*i+4*j;
                if(!isfinite(p[0])||!isfinite(p[1])||!isfinite(p[2]))ok=false;
            }
        }
        for(u32 i=0;ok && i<h.groups;i++){
            const u32 *key=(const u32*)(data+check+64*i);
            if(!key[0] || key[0]>h.triangles || key[1]>=3*key[0] || key[2]>=3*key[0] || key[3]>=3*key[0])ok=false;
        }
        memset(data+active,0,4ull*h.groups);
        vkUnmapMemory(g_vk.dev,props.data.memory);
    }
    fclose(f);
    if(!ok)return false;
    props.bottom.geometry=(VkAccelerationStructureGeometryKHR){.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry.triangles={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat=VK_FORMAT_R32G32B32_SFLOAT,.vertexData.deviceAddress=props.data.address,.vertexStride=16,
            .maxVertex=3*h.triangles-1,.indexType=VK_INDEX_TYPE_NONE_KHR}};
    if(!contact_tree(&props.bottom,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,h.triangles))return false;
    u64 address=props.data.address;
    props.info=(PropInfo){.vertices=address,.tags=address+tags,.atlas=address+atlas,.checks=address+check,
        .active=address+active,.triangles=h.triangles,.groups=h.groups,.properties=h.version==2?address+properties:0};
    props.activeOffset=active;
    return true;
}
