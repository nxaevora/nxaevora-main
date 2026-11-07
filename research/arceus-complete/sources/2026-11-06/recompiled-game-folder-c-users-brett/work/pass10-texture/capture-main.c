static struct { Surf *head,*tail; } g_lru[5];
#define LRU_AGED 2
#include "surface-capture-tested.inc"
static void link_test(Surf* s,int list) {
 s->lru=list;s->lru_prev=g_lru[list].tail;s->lru_next=NULL;
 if(s->lru_prev)s->lru_prev->lru_next=s;else g_lru[list].head=s;g_lru[list].tail=s;
}
static void require(int v,const char* msg) {if(!v)fatal("FAIL %s\n",msg);}
typedef struct {Surf s;u8* expected;u8* stencil;u32 bpp;u64 size;VkImageLayout layout;VkPipelineStageFlags2 stage;VkAccessFlags2 access;} Fixture;
static Fixture fixtures[12];
static void make_fixture(int k,VkFormat format,u32 w,u32 h,u32 layers,u32 bpp,int depth,int stencil) {
 Fixture* f=&fixtures[k];Surf* s=&f->s;
 *s=(Surf){.gva=0x10000000ull+k*0x100000,.w=w,.h=h,.sw=w,.sh=h,.layers=layers,.levels=1,.depth3d=1,
   .fkey=(u32)k,.f={.vk=format,.bpp=(u8)bpp,.bw=1,.bh=1,.depth=(u8)depth,.stencil=(u8)stencil},
   .is_rt=1,.type=layers>1?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D,.last_used=100,.drawn_at=100,
   .wgen=100+k,.wseq=1000+k};
 f->bpp=bpp;f->size=(u64)w*h*layers*bpp;f->expected=malloc(f->size);
 for(u64 i=0;i<f->size;i++)f->expected[i]=(u8)(i*73+(i>>3)*17+k*29);
 if(depth)for(u64 i=0;i<f->size/4;i++) {
   if(format==VK_FORMAT_D32_SFLOAT){float v=(float)(i%257)/256.0f;memcpy(f->expected+i*4,&v,4);}
   else {u32 v=(u32)(i*65537u+17u)&0xffffff;memcpy(f->expected+i*4,&v,4);}
 }
 VkImageCreateInfo ii={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=format,
   .extent={w,h,1},.mipLevels=1,.arrayLayers=layers,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
   .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
 require(vk_image_create(&ii,&s->img,&s->mem),"fixture image create");
 vk_surf_use(vk_cmd(),s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_2_COPY_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT);
 u64 at;VkBuffer buf;memcpy(vk_stage(f->size,vk_stage_align(),&buf,&at),f->expected,f->size);
 VkBufferImageCopy copy={.bufferOffset=at,.imageSubresource={depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,0,layers},.imageExtent={w,h,1}};
 rec_CmdCopyBufferToImage(vk_cmd(),buf,s->img,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
 if(stencil) {
   u64 n=(u64)w*h*layers;f->stencil=malloc(n);for(u64 i=0;i<n;i++)f->stencil[i]=(u8)(i*31+19);
   memcpy(vk_stage(n,vk_stage_align(),&buf,&at),f->stencil,n);copy.bufferOffset=at;copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;
   rec_CmdCopyBufferToImage(vk_cmd(),buf,s->img,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
 }
 f->layout=k%3==0?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:k%3==1?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;
 f->stage=k%3==1?VK_PIPELINE_STAGE_2_BLIT_BIT:VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
 f->access=k%3==1?VK_ACCESS_2_TRANSFER_READ_BIT:VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
 vk_surf_use(vk_cmd(),s,f->layout,f->stage,f->access);
 if(k==2){s->stage=f->stage=0;s->access=f->access=0;}
 if(k==3)s->f.bpp=16; /* decoded BC5: source block bytes differ from actual Vulkan pixel bytes */
 if(k==11){s->is_rt=0;s->took_drawn=1;}
 link_test(s,s->is_rt?VK_LRU_RT:VK_LRU_TEX);
 char path[256];snprintf(path,sizeof path,"work/pass10-texture/capture/expected-%d.bin",k);
 FILE* out=fopen(path,"wb");require(out!=NULL,"expected output open");require(fwrite(f->expected,1,f->size,out)==f->size,"expected write");fclose(out);
}
static void check_source(Fixture* f,int id) {
 Surf* s=&f->s;require(s->layout==f->layout && s->stage==f->stage && s->access==f->access,"tracked layout/stage/access restored exactly");
 require(s->wgen==(u32)(100+id)&&s->wseq==(u64)(1000+id)&&s->last_used==100,"source write identity/recency unchanged");
 vk_surf_use(vk_cmd(),s,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_2_COPY_BIT,VK_ACCESS_2_TRANSFER_READ_BIT);
 VkBuffer rb;u8* got=vk_readback_buffer(f->size,&rb);VkBufferImageCopy copy={.imageSubresource={s->f.depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,0,s->layers},.imageExtent={s->sw,s->sh,1}};
 rec_CmdCopyImageToBuffer(vk_cmd(),s->img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,rb,1,&copy);vk_finish();vk_readback_ready();
 for(u64 i=0;i<f->size;i++)if(!(s->f.vk==VK_FORMAT_D24_UNORM_S8_UINT && i%4==3))require(got[i]==f->expected[i],"source bytes retained after capture");
 if(f->stencil){u64 n=f->size/4;got=vk_readback_buffer(n,&rb);copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;rec_CmdCopyImageToBuffer(vk_cmd(),s->img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,rb,1,&copy);vk_finish();vk_readback_ready();require(!memcmp(got,f->stencil,n),"source stencil retained after depth-only capture");}
 printf("PASS source%d VkFormat%u %ux%u layers%u: source bytes/state unchanged\n",id,(u32)s->f.vk,s->sw,s->sh,s->layers);
}
static void eligibility_checks(void) {
 Surf base=fixtures[0].s;u32 bpp;u64 size;VkImageAspectFlags aspect;unsigned n=0;
 #define REJECT(change) do {Surf s=base;change;require(!diagnostic_surface_size(&s,100,&bpp,&aspect,&size),"eligibility rejection");n++;}while(0)
 REJECT(s.img=VK_NULL_HANDLE);REJECT(s.is_rt=0;s.took_drawn=0);REJECT(s.last_used=97);REJECT(s.last_used=101);
 REJECT(s.sw=0);REJECT(s.sh=0);REJECT(s.layers=0);REJECT(s.layout=VK_IMAGE_LAYOUT_UNDEFINED);
 REJECT(s.pred_undefined=1);REJECT(s.volume=1);REJECT(s.type=VK_IMAGE_VIEW_TYPE_3D);REJECT(s.depth3d=2);
 REJECT(s.type=VK_IMAGE_VIEW_TYPE_1D);REJECT(s.f.vk=VK_FORMAT_BC5_UNORM_BLOCK);REJECT(s.sw=0xffffffff);REJECT(s.layers=0xffffffff);
 {Surf s=base;s.sw=4096;s.sh=4096;s.layers=1;require(diagnostic_surface_size(&s,100,&bpp,&aspect,&size)&&size==(64ull<<20),"exact64MiB permitted");n++;s.sh++;require(!diagnostic_surface_size(&s,100,&bpp,&aspect,&size),"above64MiB rejected");n++;}
 {Surf s=base;s.last_used=98;require(diagnostic_surface_size(&s,100,&bpp,&aspect,&size),"age2 accepted");n++;}
 {Surf s=base;s.last_used=0xffffffff;require(diagnostic_surface_size(&s,0,&bpp,&aspect,&size),"present wrap age1 accepted");n++;}
 printf("PASS eligibility/overflow/per-image-bound checks%u\n",n);
 #undef REJECT
}
int main(void) {
 init();g_vk_present=100;
 make_fixture(0,VK_FORMAT_R8G8B8A8_UNORM,37,19,1,4,0,0);
 make_fixture(1,VK_FORMAT_R8G8B8A8_SRGB,29,11,1,4,0,0);
 make_fixture(2,VK_FORMAT_R8_UINT,17,31,2,1,0,0);
 make_fixture(3,VK_FORMAT_R16G16_UNORM,13,7,2,4,0,0);
 make_fixture(4,VK_FORMAT_R16G16B16A16_SFLOAT,13,7,1,8,0,0);
 make_fixture(5,VK_FORMAT_R32_SFLOAT,19,9,1,4,0,0);
 make_fixture(6,VK_FORMAT_R32G32_SFLOAT,11,13,1,8,0,0);
 make_fixture(7,VK_FORMAT_R32G32B32A32_SFLOAT,10,5,2,16,0,0);
 make_fixture(8,VK_FORMAT_D32_SFLOAT,17,11,1,4,1,0);
 make_fixture(9,VK_FORMAT_D24_UNORM_S8_UINT,13,9,2,4,1,1);
 make_fixture(10,VK_FORMAT_R16_SFLOAT,31,7,1,2,0,0);
 make_fixture(11,VK_FORMAT_R8G8B8A8_UNORM,23,11,1,4,0,0);
 eligibility_checks();
 vk_surf_diagnostic_capture("work/pass10-texture/capture/snapshot");
 for(int i=0;i<12;i++)check_source(&fixtures[i],i);
 puts("PASS capture GPU source checks; external manifest verifier must check savedbytes and layering.");
 return 0;
}
