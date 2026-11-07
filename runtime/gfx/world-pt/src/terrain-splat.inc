typedef struct {VkDeviceAddress data;u32 side,banks;} SplatInfo;
typedef struct {ContactBuffer buffer;SplatInfo gpu;bool attempted;} SplatArea;
static SplatArea splats[12];
static bool splat_load(SplatArea *area,u32 id,u32 archive){
    if(area->attempted)return area->gpu.data!=0;
    area->attempted=true;
    const char *dir=getenv("ARCEUS_TERRAIN_DIR");char path[1024];
    if(!dir || snprintf(path,sizeof path,"%s/ha_area%02u.splat",dir,id)>=(int)sizeof path)return false;
    FILE *f=fopen(path,"rb");if(!f)return false;
    struct {char magic[4];u32 version,archive,side,banks,mask;} h;
    bool ok=fread(&h,sizeof h,1,f)==1 && !memcmp(h.magic,"SPLT",4) && (h.version==1 || h.version==2) && h.archive==archive &&
        (h.side==1024 || h.side==2048) && (h.banks==1 || h.banks==2) && h.mask && !(h.mask>>(4*h.banks));
    if(!ok){fclose(f);return false;}
    u32 layers=h.banks*4;
    size_t bytes=layers*(32ull+4ull*21845)+4ull*h.banks*h.side*h.side;
    if(h.version==2)bytes+=layers*43691ull*4;
    void *mapped=NULL;
    ok=contact_buffer(&area->buffer,bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true) &&
        vkMapMemory(g_vk.dev,area->buffer.memory,0,VK_WHOLE_SIZE,0,&mapped)==VK_SUCCESS;
    if(ok){
        ok=fread(mapped,1,bytes,f)==bytes && fgetc(f)==EOF;
        for(u32 i=0;ok && i<layers;i++){
            const float *p=(const float*)mapped+8*i;
            for(u32 k=0;k<8;k++)if(!isfinite(p[k]))ok=false;
            if((p[6]!=0 && (h.version<2 || p[6]!=1 || i==0)) ||
               (p[7]!=0 && (h.version<2 || p[7]!=1 || p[6]!=0)))ok=false;
            if(!(p[0]>0 && p[1]>0 && p[4]>=0 && p[4]<=1 && p[5]==(float)((h.mask>>i)&1)))ok=false;
        }
        vkUnmapMemory(g_vk.dev,area->buffer.memory);
    }
    fclose(f);
    if(!ok){grove_free_buffer(&area->buffer);area->buffer=(ContactBuffer){0};return false;}
    area->gpu=(SplatInfo){area->buffer.address,h.side,h.banks};
    LOG("world-pt: ha_area%02u terrain material atlas %ux%u, native layer mask %x\n",id,h.side,h.side,h.mask);
    return true;
}
