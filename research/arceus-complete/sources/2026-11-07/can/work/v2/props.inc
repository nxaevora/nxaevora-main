typedef struct {
    u64 vertices,tags,atlas,checks,active;
    u32 triangles,groups;
    u64 properties;u32 firstPlaced,classCount;u64 frames,normalMaps,emissionData;u32 emissionBanks,lightTriangles;
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
    bool ok=fread(&h,sizeof h,1,f)==1 && !memcmp(h.magic,"PROP",4) && (h.version>=1 && h.version<=8) &&
        h.archive==archiveSize && h.triangles>0 && h.triangles<=1000000 &&
        h.materials>0 && h.materials<=255 && h.groups>0 && h.groups<=1024;
    u32 firstPlaced=h.groups;
    if(ok && h.version>=3)ok=fread(&firstPlaced,sizeof firstPlaced,1,f)==1 && firstPlaced<=h.groups;
    u32 mapCount=0;
    if(ok && h.version>=5)ok=fread(&mapCount,sizeof mapCount,1,f)==1 && mapCount>0 && mapCount<=1024;
    u32 emitCount=0;
    if(ok && h.version>=6)ok=fread(&emitCount,sizeof emitCount,1,f)==1 && emitCount>0 && emitCount<=255;
    u32 lightCount=0;
    if(ok && h.version>=7)ok=fread(&lightCount,sizeof lightCount,1,f)==1 && lightCount>0 && lightCount<=h.triangles;
    u32 detailWords=0;
    if(ok && h.version==8)ok=fread(&detailWords,sizeof detailWords,1,f)==1 &&
        detailWords>=4u*emitCount && detailWords<=16u*1024u*1024u;
    if(!ok){fclose(f);return false;}
    u32 classes=h.version==4?h.materials:0;
    u64 tags=48ull*h.triangles,atlas=tags+4ull*h.triangles;
    u64 properties=atlas+16384ull*h.materials;
    u64 payload=properties+(h.version>=2?16384ull*h.materials:0);
    u64 check=(payload+15)&~15ull,active=check+64ull*h.groups;
    u64 frames=(active+4ull*(h.groups+classes)+7)&~7ull;
    u64 maps=frames+(h.version>=5?24ull*h.triangles:0);
    u64 emit=maps+mapCount*(4ull+128*128*2);
    u64 cdf=emit+(emitCount?16ull*h.groups+emitCount*128ull*128*8:0);
    u64 detail=cdf+(lightCount?8ull*lightCount+4:0),total=detail+4ull*detailWords;
    VkBufferUsageFlags usage=VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    void *mapped=NULL;
    ok=contact_buffer(&props->data,total,usage,true) &&
       vkMapMemory(g_vk.dev,props->data.memory,0,VK_WHOLE_SIZE,0,&mapped)==VK_SUCCESS;
    if(ok){
        u8 *data=mapped;
        ok=fread(data,1,payload,f)==payload &&
            fread(data+check,64,h.groups,f)==h.groups;
        if(ok && h.version>=5){
            ok=fread(data+frames,1,total-frames,f)==total-frames;
            for(u32 i=0;ok && i<mapCount;i++){
                float strength=((float*)(data+maps))[i*8193];
                ok=isfinite(strength) && strength>0.f && strength<=4.f;
            }
        }
        if(ok && emitCount){
            const u32 *records=(const u32*)(data+emit);
            for(u32 i=0;ok && i<h.groups;i++){
                const u32 *row=records+4*i;
                float scale;memcpy(&scale,row+1,sizeof(scale));
                ok=row[0]<=emitCount && isfinite(scale) && scale>=0.f && scale<=64.f &&
                    ((row[0]!=0)==(scale>0.f)) && row[2]==0u && row[3]<=lightCount && (!lightCount || ((row[3]!=0)==(row[0]!=0)));
            }
            const u16 *pixels=(const u16*)(records+4*h.groups);
            for(u32 i=0;ok && i<emitCount*128u*128u*4u;i++)
                if(pixels[i]>0x3c00u)ok=false; /* finite unsigned linear light, 0..1 */
        }
        if(ok && lightCount){
            const u32 *pairs=(const u32*)(data+cdf),*records=(const u32*)(data+emit),*tag=(const u32*)(data+tags);
            float last=0.f;u32 covered=0;
            for(u32 i=0;ok && i<lightCount;i++){
                u32 tri=pairs[2*i];float weight;memcpy(&weight,pairs+2*i+1,4);
                if(tri>=h.triangles || (i && tri<=pairs[2*i-2]) || !isfinite(weight) || weight<last || weight>1.f){ok=false;break;}
                u32 group=tag[tri]>>16;if(group>=h.groups || !records[4*group]){ok=false;break;}
                const u32 *key=(const u32*)(data+check+64*group);
                float begin=((const float*)key)[11];
                if(!isfinite(begin) || begin<1.f || begin>3.f*h.triangles || (u32)(begin-1)%3u){ok=false;break;}
                u32 first=(u32)(begin-1)/3u;
                if(tri<first || tri-first>=key[0] || records[4*group+3]+tri-first!=i+1){ok=false;break;}
                last=weight;
            }
            for(u32 j=0;ok && j<h.groups;j++)if(records[4*j])covered+=*(const u32*)(data+check+64*j);
            if(last!=1.f || covered!=lightCount || pairs[2*lightCount]!=0)ok=false;
        }
        if(ok && detailWords){
            const u32 *table=(const u32*)(data+detail);
            u64 cursor=4ull*emitCount;
            for(u32 i=0;ok && i<emitCount;i++){
                const u32 *entry=table+4*i;u32 width=entry[1],height=entry[2];
                ok=entry[0]==cursor && width>0 && width<=4096 && height>0 && height<=4096 && entry[3]==0;
                cursor+=2ull*width*height;
            }
            ok=ok && cursor==detailWords;
            const u16 *pixels=(const u16*)(table+4u*emitCount);
            for(u64 i=0;ok && i<2ull*(detailWords-4u*emitCount);i++)if(pixels[i]>0x3c00u)ok=false;
            if(ok){
                u32 *groups=(u32*)(data+emit);
                for(u32 i=0;i<h.groups;i++)if(groups[4*i])groups[4*i]|=0x80000000u;
            }
        }
        if(ok)ok=fgetc(f)==EOF;
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
            if(h.version>=5){
                float bank=((const float*)key)[15];
                if(!isfinite(bank) || bank<0 || bank>mapCount || floorf(bank)!=bank)ok=false;
            }
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
        .active=address+active,.triangles=h.triangles,.groups=h.groups,.properties=h.version>=2?address+properties:0,.firstPlaced=firstPlaced,.classCount=classes,
        .frames=h.version>=5?address+frames:0,.normalMaps=h.version>=5?address+maps:0,
        .emissionData=emitCount?address+emit:0,.emissionBanks=emitCount,.lightTriangles=lightCount};
    props->activeOffset=active;
    return true;
}
