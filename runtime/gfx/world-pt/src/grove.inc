enum {groveShapeLimit=128,groveInstanceLimit=8192};
typedef struct {u32 start,count,cut,pbr,anchor[4];} GroveShape;
typedef struct {VkTransformMatrixKHR transform;u32 shape,pad[3];} GrovePlacement;
typedef struct {
    VkDeviceAddress shapes,placements,vertices,colors,active;
    u32 shapeCount,instanceCount;
} GroveInfo;
typedef struct {
    GroveInfo info;
    ContactBuffer data;
    ContactTree *trees;
    VkAccelerationStructureInstanceKHR *instances;
    VkDeviceSize activeOffset;
    bool attempted,built;
} GroveArea;
static GroveArea groves[12];
_Static_assert(sizeof(GrovePlacement)==64 && sizeof(GroveShape)==32 && sizeof(GroveInfo)==48,"grove storage layout");
static void grove_free_buffer(ContactBuffer *b){
    if(b->handle)vkDestroyBuffer(g_vk.dev,b->handle,NULL);
    if(b->memory)vkFreeMemory(g_vk.dev,b->memory,NULL);
}
static bool grove_load(GroveArea *area,u32 id,u32 source){
    if(area->attempted)return area->info.instanceCount!=0;
    area->attempted=true;
    char path[1024];const char *dir=getenv("ARCEUS_TERRAIN_DIR");
    if(!dir || snprintf(path,sizeof path,"%s/ha_area%02u.grove",dir,id)>=(int)sizeof path)return false;
    FILE *f=fopen(path,"rb");
    if(!f)return false;
    struct {char magic[4];u32 version,archive,shapes,instances,tris;} h;
    if(fread(&h,sizeof h,1,f)!=1 || memcmp(h.magic,"GRVE",4) || (h.version<1 || h.version>3) || h.archive!=source ||
       !h.shapes || h.shapes>groveShapeLimit || !h.instances || h.instances>groveInstanceLimit ||
       !h.tris || h.tris>1000000){fclose(f);return false;}
    size_t placements=h.shapes*sizeof(GroveShape),vertices=placements+h.instances*sizeof(GrovePlacement);
    size_t colors=vertices+48ull*h.tris,active=colors+(h.version>=2?32768ull:16384ull)*h.shapes;
    size_t frameOffset=active,mapOffset=frameOffset;
    if(h.version==3){mapOffset+=24ull*h.tris;active=mapOffset+32772ull*h.shapes;}
    VkBufferUsageFlags usage=VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    void *mapped=NULL;
    bool ok=contact_buffer(&area->data,active+4ull*(h.shapes+h.instances),usage,true) &&
        vkMapMemory(g_vk.dev,area->data.memory,0,VK_WHOLE_SIZE,0,&mapped)==VK_SUCCESS;
    if(ok)ok=fread(mapped,1,active,f)==active && fgetc(f)==EOF;
    fclose(f);
    GroveShape *shape=mapped;
    area->trees=calloc(h.shapes,sizeof *area->trees);
    area->instances=calloc(h.instances,sizeof *area->instances);
    ok=ok && area->trees && area->instances;
    u32 end=0;
    for(u32 i=0;ok && i<h.shapes;i++){
        GroveShape *s=&shape[i];
        ok=s->start==end && s->count>0 && s->count<16384 && end<=h.tris && s->count<=h.tris-end &&
            s->cut<=255 && (s->pbr&~(0xffffu|(h.version>=2?0x80000000u:0u)|(h.version==3?0x40000000u:0u)))==0u;
        if(h.version==3){float strength;memcpy(&strength,(u8*)mapped+mapOffset+32772ull*i,4);
            if(!isfinite(strength) || strength<0.f || strength>4.f)ok=false;}
        for(u32 j=0;ok && j<4;j++)ok=s->anchor[j]<3*s->count;
        end+=s->count;
    }
    ok=ok && end==h.tris;
    for(u32 i=0;ok && i<h.tris*3;i++){
        const float *v=(const float*)((u8*)mapped+vertices)+4*i;
        ok=isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
    }
    for(u32 i=0;ok && i<h.shapes;i++){
        ContactTree *tree=&area->trees[i];GroveShape *s=&shape[i];
        tree->geometry=(VkAccelerationStructureGeometryKHR){.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .geometry.triangles={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                .vertexFormat=VK_FORMAT_R32G32B32_SFLOAT,.vertexData.deviceAddress=area->data.address+vertices+48ull*s->start,
                .vertexStride=16,.maxVertex=3*s->count-1,.indexType=VK_INDEX_TYPE_NONE_KHR}};
        ok=contact_tree(tree,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,s->count);
    }
    for(u32 i=0;ok && i<h.instances;i++){
        const GrovePlacement *p=(const GrovePlacement*)((u8*)mapped+placements)+i;
        for(u32 r=0;r<3;r++)for(u32 c=0;c<4;c++)if(!isfinite(p->transform.matrix[r][c]))ok=false;
        if(p->shape>=h.shapes || p->pad[0]>(u32)(h.version>=2) || p->pad[1] || p->pad[2]){ok=false;break;}
        area->instances[i]=(VkAccelerationStructureInstanceKHR){.transform=p->transform,.instanceCustomIndex=2+i,.mask=255,
            .flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,.accelerationStructureReference=contact_address(&area->trees[p->shape])};
    }
    if(mapped)vkUnmapMemory(g_vk.dev,area->data.memory);
    if(!ok){
        if(area->trees)for(u32 i=0;i<h.shapes;i++){
            ContactTree *tree=&area->trees[i];
            if(tree->handle)vkDestroyAccelerationStructureKHR(g_vk.dev,tree->handle,NULL);
            grove_free_buffer(&tree->scratch);grove_free_buffer(&tree->storage);
        }
        grove_free_buffer(&area->data);free(area->trees);free(area->instances);
        area->trees=NULL;area->instances=NULL;return false;
    }
    VkDeviceAddress base=area->data.address;
    area->info=(GroveInfo){base,base+placements,base+vertices,base+colors,base+active,h.shapes,h.instances};
    area->activeOffset=active;return true;
}
