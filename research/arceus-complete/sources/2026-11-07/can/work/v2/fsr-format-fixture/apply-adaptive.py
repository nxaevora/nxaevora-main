from pathlib import Path
p=Path("work/v2/terrain.inc")
s=p.read_text().replace("typedef struct {ContactBuffer vertex;u32 archiveBytes;int attempted;} LandArea;",
"""typedef struct {
    ContactBuffer vertex;
    VkDeviceAddress index;
    u32 vertexCount,triangleCount,archiveBytes;
    bool attempted;
} LandArea;""").replace("PropInfo props;} LandInfo;","PropInfo props;VkDeviceAddress index;u32 triangles,pad;} LandInfo;")
s=s.replace("sizeof(LandInfo)==96","offsetof(LandInfo,index)==96 && sizeof(LandInfo)==112")
a=s.index("static bool land_load(")
b=s.index("    land.bottom.geometry=",a)
s=s[:a]+'''typedef struct {float xyz[3];u32 color;} LandVertex;
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
    const bool regular=h.version==2;
    if(regular){
        if(h.a!=TERRAIN_SIDE || h.b!=TERRAIN_SIDE || !land_regular_indices())return false;
    }else if(h.version!=3 || h.a<3 || h.a>TERRAIN_TRIS+2 || h.b<1 || h.b>TERRAIN_TRIS)return false;
    u32 nv=regular?TERRAIN_SIDE*TERRAIN_SIDE:h.a,nt=regular?TERRAIN_TRIS:h.b;
    size_t vertexBytes=nv*sizeof(LandVertex),indexWords=regular?0:3u*nt;
    size_t bytes=vertexBytes+indexWords*sizeof(u32);
    LandVertex *data=malloc(bytes);
    if(!data)return false;
    u32 *indices=(u32*)(data+nv);
    bool ok=regular?land_regular_data(file,data):
        fread(data,sizeof(LandVertex),nv,file)==nv && fread(indices,sizeof(u32),indexWords,file)==indexWords;
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
'''+s[b:]
s=s.replace(".maxVertex=TERRAIN_SIDE*TERRAIN_SIDE-1,.indexType=VK_INDEX_TYPE_UINT32,.indexData.deviceAddress=land.indices.address",
            ".maxVertex=TERRAIN_TRIS+1,.indexType=VK_INDEX_TYPE_UINT32,.indexData.deviceAddress=area->index")
s=s.replace("    bool haveProps=id==0","    land.bottom.geometry.geometry.triangles.indexData.deviceAddress=area->index;\n    land.bottom.geometry.geometry.triangles.maxVertex=area->vertexCount-1;\n    land.bottom.range.primitiveCount=area->triangleCount;\n    bool haveProps=id==0")
s=s.replace(".width=TERRAIN_SIDE,.enabled=1", ".index=area->index,.triangles=area->triangleCount,.enabled=1").replace('id,TERRAIN_TRIS);','id,area->triangleCount);')
p.write_text(s)
p=Path("work/v2/contact.comp");s=p.read_text().replace("vec2 jitter;PropInfo props;};","vec2 jitter;PropInfo props;Words indices;uint triangles;uint pad;};")
s=s.replace("2u*(u.draws.land.side-1u)*(u.draws.land.side-1u)","u.draws.land.triangles")
a=s.index("    uint tri=primitive&~landFlag,side=");b=s.index("    return normalize(fromWorldDirection(n));",a)
s=s[:a]+'''    uvec3 ids=landTriangle(primitive);
    Vertices v=u.draws.land.vertices;
    vec3 a=v.v[ids.x].xyz;
    vec3 n=cross(v.v[ids.y].xyz-a,v.v[ids.z].xyz-a);
'''+s[b:]
a=s.index("        uint tri=primitive&~landFlag,side=");b=s.index("        Vertices mesh=",a)
s=s[:a]+"        uvec3 ids=landTriangle(primitive);\n"+s[b:]
s=s.replace("vec3 faceNormal(uint primitive){", """uvec3 landTriangle(uint primitive){
    uint first=3u*(primitive&~landFlag);
    Words idx=u.draws.land.indices;
    return uvec3(idx.v[first],idx.v[first+1u],idx.v[first+2u]);
}
vec3 faceNormal(uint primitive){""")
p.write_text(s)
