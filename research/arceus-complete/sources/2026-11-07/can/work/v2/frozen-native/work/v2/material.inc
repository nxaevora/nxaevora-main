enum { worldTextureSlots=WORLD_TEXTURE_SLOTS, worldDrawLimit=2048 };
#include "material-layouts.inc"
typedef struct {
    u32 end, slot, roughness, metallic;
    float uv[4];
    u32 emission;
    float emissionScale;
    u32 base,sourceStart;
    u64 fallbackColor;u32 wrap,pad;
} WorldDrawMaterial;
_Static_assert(sizeof(WorldDrawMaterial)==64,"draw material GPU layout");
enum {worldTintBytes=64};
typedef struct {
    u64 address;
    VkImage image;
    u32 tic[8],tsc[8];
    ShTex shader;
} WorldTexture;
enum { maxCapturedTextures=4*worldDrawLimit+1 };
static WorldTexture capturedTextures[maxCapturedTextures];
static u16 textureBinding[worldTextureSlots];
static u32 capturedTextureCount;
static WorldDrawMaterial draws[worldDrawLimit];
static u32 textureCount,drawCount,captureDrawCount;
static WorldDrawMaterial capturedDraws[worldDrawLimit];
static struct {VkImage albedo,depth;VkRect2D rect;} captureTargets[worldDrawLimit];
static ContactBuffer drawTable;

static u32 world_texture(const VkSh* shader,const TexRes* bound,u32 binding,bool srgb){
    if(!shader)return 0;
    for(int k=0;k<shader->si.ntex && k<SH_MAXTEX;k++){
        const ShTex *st=&shader->si.tex[k];
        const TexRes *x=&bound[k];
        if(st->cbuf_off!=binding || vk_tex_view_type(st)!=VK_IMAGE_VIEW_TYPE_2D || !x->have ||
           !x->t || x->t->is_rt || x->t->f.depth || !!tex_srgb(x->tic,x->tsc)!=srgb)continue;
        u32 i=1;
        while(i<=capturedTextureCount){
            WorldTexture *t=&capturedTextures[i];
            if(t->image==x->t->img && t->address==x->t->gva &&
               !memcmp(t->tic,x->tic,sizeof t->tic) && !memcmp(t->tsc,x->tsc,sizeof t->tsc))return i;
            i++;
        }
        if(i==maxCapturedTextures)break;
        WorldTexture *t=&capturedTextures[++capturedTextureCount];
        t->address=x->t->gva;t->image=x->t->img;t->shader=*st;
        memcpy(t->tic,x->tic,sizeof t->tic);memcpy(t->tsc,x->tsc,sizeof t->tsc);
        return i;
    }
    return 0;
}
static void material_range(GpuChannel* ch,const VkSh *shader,const TexRes *bound,u32 end){
    if(shader && (g_vk_present==600 || g_vk_present==1800) && getenv("ARCEUS_MATERIAL_SAMPLE")){
        for(int k=0;k<shader->si.ntex && k<SH_MAXTEX;k++){
            Surf *s=bound[k].t;
            if(!bound[k].have || !s || s->is_rt || s->size<2048 || vk_buf_pending(s->gva,2048) ||
               vk_compute_pending(s->gva,2048))continue;
            const u8 *bytes=gmem_host(s->gva,2048);
            if(!bytes)continue;
            u64 signature=14695981039346656037ull;
            for(u32 j=0;j<2048;j++)signature=(signature^bytes[j])*1099511628211ull;
            LOG("world-mat: frame=%u draw=%u fs=%016llx cb=%x size=%ux%u signature=%016llx\n",
                g_vk_present,captureDrawCount,(unsigned long long)shader->hash,shader->si.tex[k].cbuf_off,s->w,s->h,(unsigned long long)signature);
        }
    }
    WorldDrawMaterial *draw=&capturedDraws[captureDrawCount++];
    *draw=(WorldDrawMaterial){.end=end,.slot=world_texture(shader,bound,8,true)<<16,.uv={1,1,0,0}};
    if(shader && shader->hash==0xea4d86b0272a581full)draw->slot|=0x80000000u;
    if(shader)switch(shader->hash){
        case 0xa336734bf0d4c9c0ull: case 0x66ef61705e89a2e7ull: case 0xe1a792dc6d8fac9eull:
            draw->metallic=world_texture(shader,bound,0xa,false);
            /* Both families read a separate roughness map as well. */
            /* fall through */
        case 0x945bb82325ec366bull: case 0xf12efd26c1fd6003ull: case 0xa8dc689732b77bb1ull:
        case 0xec290e81a58cf553ull: case 0x4a3a14abfea6e435ull: case 0xc17b85198f247139ull:
            draw->roughness=world_texture(shader,bound,0x10,false);break;
        case 0xb353d4fdae52bb03ull:
            draw->roughness=world_texture(shader,bound,0x24,false);
            draw->metallic=draw->roughness;break;
    }
    unsigned layout=shader?world_material_layout(shader->hash):0;
    if(shader && shader->hash==0xa336734bf0d4c9c0ull && ch->cbind[4][7] && cb_range(ch,4,shader,7)>=0x7c){
        u64 addr=ch->cbind[4][7]+0x78;
        if(!vk_buf_pending(addr,4) && !vk_compute_pending(addr,4)){
            const void *p=gmem_host(addr,4);
            float power=0;
            if(p)memcpy(&power,p,4);
            if(power>0 && power<=64.f){
                draw->emission=world_texture(shader,bound,0x12,true);
                draw->emissionScale=power;
            }
        }
    }
    if((layout&1) && ch->cbind[4][8] && cb_range(ch,4,shader,8)>=32){
        u64 addr=ch->cbind[4][8]+16;
        if(!vk_buf_pending(addr,16) && !vk_compute_pending(addr,16)){
            float p[4];const void *source=gmem_host(addr,16);if(!source)return;memcpy(p,source,16);
            if(isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]) && isfinite(p[3]))memcpy(draw->uv,p,16);
        }
    }
    if((layout&4)!=0){
        draw->slot|=128u<<8;

    }else if((layout&2) && ch->cbind[4][7] && cb_range(ch,4,shader,7)>=28){
        u64 address=ch->cbind[4][7]+0x18;
        if(!vk_buf_pending(address,4) && !vk_compute_pending(address,4)){
            const void *ptr=gmem_host(address,4);
            float cutoff=0;
            if(ptr)memcpy(&cutoff,ptr,sizeof cutoff);
            if(cutoff>0 && cutoff<=1){
                draw->slot|=(u32)lroundf(cutoff*255.f)<<8;
        
            }
        }
    }
}
typedef struct {u32 id,role,triangles;} TextureChoice;
static int compare_texture_choices(const void *a,const void *b){
    const TextureChoice *x=a,*y=b;
    if(x->role!=y->role)return x->role>y->role?-1:1;
    if(x->triangles!=y->triangles)return x->triangles>y->triangles?-1:1;
    return x->id<y->id?-1:x->id>y->id;
}
#include "material-tile.inc"
static u16 baseTextureId[worldDrawLimit];
static void select_world_textures(void){
    static TextureChoice choices[maxCapturedTextures];
    static u16 physical[maxCapturedTextures];
    size_t count=capturedTextureCount+1;
    memset(choices,0,count*sizeof(*choices));memset(physical,0,count*sizeof(*physical));
    u32 first=0;
    for(u32 j=0;j<drawCount;j++){
        WorldDrawMaterial *d=draws+j;
        u32 ids[]={(d->slot>>16)&0x7fff,d->roughness,d->metallic,d->emission};
        u32 roles[]={d->slot&0xff00?4:3,2,1,5};
        for(u32 k=0;k<4;k++){
            if(!ids[k])continue;
            TextureChoice *c=&choices[ids[k]];
            c->id=ids[k];if(roles[k]>c->role)c->role=roles[k];
            c->triangles+=d->end-first;
        }
        first=d->end;
    }
    u32 used=0;
    for(u32 i=1;i<=capturedTextureCount;i++)if(choices[i].id)choices[used++]=choices[i];
    if(used>=arceus_texture_slots)qsort(choices,used,sizeof(*choices),compare_texture_choices);
    textureCount=used<arceus_texture_slots?used:arceus_texture_slots-1;
    for(u32 i=0;i<textureCount;i++){u32 id=choices[i].id;physical[id]=(u16)(i+1);textureBinding[i+1]=(u16)id;}
    for(u32 i=0;i<drawCount;i++){
        WorldDrawMaterial *d=draws+i;
        baseTextureId[i]=(d->slot>>16)&0x7fff;
        d->slot=(d->slot&0x8000ff00u)|physical[baseTextureId[i]];
        d->roughness=physical[d->roughness];d->metallic=physical[d->metallic];d->emission=physical[d->emission];
    }
}
static void bind_world_textures(VkCommandBuffer cb,VkDescriptorSet set,VkImageView dummy,VkImageLayout ro){
    VkDescriptorImageInfo infos[worldTextureSlots];
    for(u32 i=0;i<arceus_texture_slots;i++)infos[i]=(VkDescriptorImageInfo){tsamp,dummy,ro};
    for(u32 i=1;i<=textureCount;i++){
        WorldTexture *tex=&capturedTextures[textureBinding[i]];
        for(Surf *s=vk_surf_first(tex->address);s;s=s->next){
            if(s->gva!=tex->address || s->img!=tex->image || s->layout==VK_IMAGE_LAYOUT_UNDEFINED)continue;
            VkImageView view=vk_tex_bind_view(s,VK_IMAGE_VIEW_TYPE_2D,tex->tic,tex->tsc,0);
            VkSampler sampler=vk_tex_bind_sampler(tex->tsc,tex->tic,&tex->shader,0,s,VK_IMAGE_VIEW_TYPE_2D);
            if(view && sampler){
                vk_surf_touch(s);vk_surf_use(cb,s,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                infos[i]=(VkDescriptorImageInfo){sampler,view,ro};
            }
            break;
        }
    }
    tileCache.filled=tileCache.reused=tileCache.requests=tileCache.rejectedWrap=tileCache.unavailable=0;
    for(u32 j=0;j<drawCount;j++){
        if(draws[j].slot && infos[draws[j].slot&255].imageView==dummy)draws[j].slot&=0x8000ff00u;
        bool tileWitness=(g_vk_present==600 || g_vk_present==1960) && getenv("ARCEUS_QA_TILE_REFERENCE");
        if((!(draws[j].slot&255u) || tileWitness) && baseTextureId[j])
            draws[j].fallbackColor=material_color_tile(cb,&capturedTextures[baseTextureId[j]],ro,&draws[j].wrap);
        if(draws[j].roughness && infos[draws[j].roughness].imageView==dummy)draws[j].roughness=0;
        if(draws[j].metallic && infos[draws[j].metallic].imageView==dummy)draws[j].metallic=0;
        if(draws[j].emission && infos[draws[j].emission].imageView==dummy)draws[j].emission=0;
    }
    if(tileCache.filled)contact_barrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
    if(getenv("ARCEUS_RT_VERIFY") && g_vk_present%240==0)
        LOG("world-pt: overflow material tiles present=%u filled=%u reused=%u requested=%u wrapRejected=%u unavailable=%u\n",g_vk_present,tileCache.filled,tileCache.reused,tileCache.requests,tileCache.rejectedWrap,tileCache.unavailable);
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=8,
        .descriptorCount=arceus_texture_slots,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=infos};
    vkUpdateDescriptorSets(g_vk.dev,1,&write,0,NULL);
}



