#include <math.h>
#include "up_words.h"
#include "vk_dlaa.h"
#include "../motion/material_flow.h"
#include "../motion/cut_frame.h"
#include "../motion/capture.inc"
#include "nx/runtime/platform/plat.h"
#include <stdarg.h>
void vk_fsr_log(const char *fmt,...){char buf[1024];va_list a;va_start(a,fmt);vsnprintf(buf,sizeof buf,fmt,a);va_end(a);LOG("%s",buf);}
void vk_fsr_message(const char*t,bool error,const wchar_t*m){LOG_LIMIT(16,"%s error=%d: %ls\n",t,error,m);}
typedef struct {VkImage im; VmaAllocation mem; VkImageView view; VkImageLayout layout;} THist;
static THist th[2][2];
static VkDescriptorSetLayout tdsl; static VkPipelineLayout tpl; static VkPipeline tpipe; static VkSampler tsamp;
static u32 tlast,tw,thh,tindex,tjitframe; static int tready,tjitvalid;
static float tprev[16]; static u64 tzaddr;
static float up_halton(u32 x,u32 b){float v=0,a=1;while(x){a/=b;v+=a*(x%b);x/=b;}return v;}
static void tjitter(float *xy){u32 phase=1+g_vk_present%14;xy[0]=up_halton(phase,2)-.5f;xy[1]=up_halton(phase,3)-.5f;}
static int tinit(void){
 static int attempted;if(attempted)return tpipe!=0;attempted=1;
 VkFormatProperties a,bfmt;vkGetPhysicalDeviceFormatProperties(g_vk.phys,VK_FORMAT_R16G16B16A16_SFLOAT,&a);vkGetPhysicalDeviceFormatProperties(g_vk.phys,VK_FORMAT_B10G11R11_UFLOAT_PACK32,&bfmt);if(!(a.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)||!(bfmt.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))return 0;
 VkDescriptorSetLayoutBinding b[5];for(int i=0;i<5;i++) b[i]=(VkDescriptorSetLayoutBinding){i,(i==0||i==2)?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
 VkDescriptorSetLayoutCreateInfo dc={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=5,.pBindings=b};
 if(vkCreateDescriptorSetLayout(g_vk.dev,&dc,0,&tdsl))return 0;
 VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,76};
 VkPipelineLayoutCreateInfo lc={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&tdsl,.pushConstantRangeCount=1,.pPushConstantRanges=&range};
 if(vkCreatePipelineLayout(g_vk.dev,&lc,0,&tpl))return 0;
 VkSamplerCreateInfo sc={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
 if(vkCreateSampler(g_vk.dev,&sc,0,&tsamp))return 0;
 VkShaderModule m=vk_module_spv(up_words,sizeof up_words/4);if(!m)return 0;
 VkComputePipelineCreateInfo pc={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=m,.pName="main"},.layout=tpl};
 VkResult r=vkCreateComputePipelines(g_vk.dev,vk_pipeline_cache(),1,&pc,0,&tpipe);vkDestroyShaderModule(g_vk.dev,m,0);return !r;
}
static int timages(u32 w,u32 h){
 if(tw==w && thh==h)return 1;
 tw=thh=0;tready=0;temporal_snapshot.backend=NULL;
 for(int i=0;i<4;i++){THist *t=&th[i/2][i%2];if(t->im)vk_defer_image(t->im,t->mem,t->view,0);memset(t,0,sizeof *t);}
 for(int i=0;i<4;i++){THist *t=&th[i/2][i%2];VkFormat f=i==1?VK_FORMAT_R32_SFLOAT:i==2?VK_FORMAT_R16G16_SFLOAT:VK_FORMAT_R16G16B16A16_SFLOAT;
 VkImageCreateInfo c={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=f,.extent={i==3?w:1920,i==3?h:1080,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
 if(!vk_image_create(&c,&t->im,&t->mem))return 0;
 VkImageViewCreateInfo v={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=t->im,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=f,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
 if(vkCreateImageView(g_vk.dev,&v,0,&t->view))return 0;}
 tw=w;thh=h;return 1;
}
#include "../taa/temporal_camera.h"
static void tuse(VkCommandBuffer cb,THist *t,VkImageLayout l){
 vk_barrier(cb,t->im,VK_IMAGE_ASPECT_COLOR_BIT,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,VK_ACCESS_2_MEMORY_WRITE_BIT|VK_ACCESS_2_MEMORY_READ_BIT,t->layout,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT,l);t->layout=l;
}
static u32 cap_frame;static u64 cap_color,cap_depth;static float cap_matrix[16];static int cap_valid;
static struct { u64 address, write; VkImage image; } cap_material;
static FlowGpu material_flow;
static int material_init_attempted;
static u32 material_last_frame;
static u64 material_last_address;
static float material_last_jitter[2];
static int material_surface_valid(const Surf *s,u32 w,u32 h){
 return s && s->layers==1 && s->sw==w && s->sh==h &&
        s->f.vk==VK_FORMAT_R8G8B8A8_UNORM && s->layout!=VK_IMAGE_LAYOUT_UNDEFINED;
}
static Surf *up_material_surface(u32 w,u32 h){
 if(!cap_material.image)return NULL;
 for(Surf *s=vk_surf_first(cap_material.address);s;s=s->next){
  if(s->gva==cap_material.address && s->img==cap_material.image && s->wseq==cap_material.write && material_surface_valid(s,w,h))return s;
 }
 return NULL;
}
static void taa_after(GpuChannel *ch,VkSh *fs,TexRes *tex,Surf *col){
 if(!fs||!col||(fs->hash!=0x89619027ba43e5d3ull&&fs->hash!=0xe02c1a109a77fac4ull)||col->f.vk!=VK_FORMAT_B10G11R11_UFLOAT_PACK32||col->layers!=1||ch->render_pred)return;
 cap_valid=0;
 cap_material.image=VK_NULL_HANDLE;
 Surf *z=NULL;
 for(int i=0;i<fs->si.ntex&&i<SH_MAXTEX;i++)if(tex[i].have){
  Surf *s=tex[i].t;
  if(fs->si.tex[i].cbuf_off==0x32)z=s;
  if(fs->si.tex[i].cbuf_off==0x2e && material_surface_valid(s,1920,1080)){
   cap_material.image=s->img;cap_material.address=s->gva;cap_material.write=s->wseq;
  }
 }
 if(!z||z->sw!=1920||z->sh!=1080||col->sw!=1920||col->sh!=1080||z->f.vk!=VK_FORMAT_D24_UNORM_S8_UINT)return;
 u64 addr=ch->cbind[4][4];VkBuffer b;u64 off;if(!addr||cb_range(ch,4,fs,4)<256||vk_buf_pending(addr,256)||vk_compute_gpu_src(addr,256,&b,&off))return;
 const void *m=gmem_ptr(addr+192,64);if(!m)return;memcpy(cap_matrix,m,64);for(int i=0;i<16;i++)if(!isfinite(cap_matrix[i]))return;
 cap_frame=g_vk_present;cap_color=col->gva;cap_depth=z->gva;cap_valid=1;
}
static void upscale_before(VkSh *fs,TexRes *tex,Surf *target){
 static u64 previous_color, previous_color_write, previous_depth_write;
 static int disabled;up_override=0;up_address=0;
 if(disabled||!cap_valid||cap_frame!=g_vk_present||!fs||!target||target->sw!=2560||target->sh!=1440||
   (fs->hash!=0x63b3848c1a6e23e0ull && fs->hash!=0x8a160a27d90af72dull))return;
 Surf *col=NULL,*z=NULL;
 for(int i=0;i<fs->si.ntex&&i<SH_MAXTEX;i++)if(tex[i].have&&tex[i].t){
  if(tex[i].t->gva==cap_color)col=tex[i].t;
  if(tex[i].t->gva==cap_depth)z=tex[i].t;
 }
 if(!col||!z||col->sw!=1920||col->sh!=1080||z->sw!=1920||z->sh!=1080)return;
 if(tready && tlast==g_vk_present){
  if(previous_color==cap_color && tzaddr==cap_depth && previous_color_write==col->wseq &&
     previous_depth_write==z->wseq && !memcmp(cap_matrix,tprev,sizeof tprev)){
    up_override=th[1][1].view;
    up_address=cap_color;
  }
  return;
 }
 float *cur=cap_matrix;
 VkComponentMapping id={0};VkImageView cv=vk_tex_view(col,VK_IMAGE_VIEW_TYPE_2D,0,&id,0),zv=vk_tex_view(z,VK_IMAGE_VIEW_TYPE_2D,0,&id,0);
 if(!cv||!zv||!tinit()||!timages(target->sw,target->sh)){LOG_LIMIT(2,"arceus-taa: resources unavailable\n");return;}
 VkDescriptorSet set=vk_desc_alloc(tdsl);if(!set)return;
 VkCommandBuffer cb=vk_cmd();vk_render_end();
 const VkImageLayout ro=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,rw=VK_IMAGE_LAYOUT_GENERAL;
 vk_surf_use(cb,col,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
 vk_surf_use(cb,z,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
 THist *color=&th[0][0],*depth=&th[0][1],*motion=&th[1][0],*dst=&th[1][1];
 u32 w=col->sw,h=col->sh;
 for(int i=0;i<4;i++)tuse(cb,&th[i/2][i%2],rw);
 VkImageView views[]={zv,motion->view,cv,color->view,depth->view};VkDescriptorImageInfo ii[5];VkWriteDescriptorSet wr[5];
 for(int i=0;i<5;i++){int sampled=i==0||i==2;ii[i]=(VkDescriptorImageInfo){sampled?tsamp:0,views[i],sampled?ro:rw};wr[i]=(VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=sampled?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,.pImageInfo=ii+i};} vkUpdateDescriptorSets(g_vk.dev,5,wr,0,NULL);
 struct{float m[16],j[2];u32 valid;}pc={0};if(tjitvalid&&tjitframe==g_vk_present)tjitter(pc.j);
 pc.valid=tready && tlast+1==g_vk_present && previous_color==cap_color && tzaddr==z->gva && tmatrix(cur,tprev,pc.m);
 rec_CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tpipe);rec_CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tpl,0,1,&set,0,NULL);
 rec_CmdPushConstants(cb,tpl,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof pc,&pc);rec_CmdDispatch(cb,(w+7)/8,(h+7)/8,1);
 bool dl=vk_dlaa_wanted();
 VkImageLayout vendor_layout=dl?rw:ro;
 tuse(cb,color,vendor_layout);tuse(cb,depth,vendor_layout);tuse(cb,motion,rw);
 Surf *material=up_material_surface(w,h);
 VkImageView material_view=VK_NULL_HANDLE;
 VkDescriptorSet flow_set=VK_NULL_HANDLE;
 if(material && !material_init_attempted){
  material_init_attempted=1;
  const char *setting=getenv("SNX_ARCEUS_MATERIAL_MOTION");
  if(!setting || strcmp(setting,"0")){
   VkResult result=flow_create(&material_flow,g_vk.phys,g_vk.dev,w,h,vk_pipeline_cache());
   LOG("arceus-motion: material estimator initialization %d at %ux%u\n",result,w,h);
  }
 }
 if(material && material_flow.setLayout){
  material_view=vk_tex_view(material,VK_IMAGE_VIEW_TYPE_2D,0,&id,0);
  if(material_view)flow_set=vk_desc_alloc(material_flow.setLayout);
  if(flow_set){
   vk_surf_touch(material);
   vk_surf_use(cb,material,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  }
 }
 vk_rec_sync(); /* finish all queued records before the vendor directly records to this same command buffer */
 static u64 prevtime;u64 now=plat_ns();float ms=prevtime?(float)(now-prevtime)*1e-6f:8.333f;prevtime=now;
 VkFsr2Frame f={.color=color->im,.depth=depth->im,.mv=motion->im,.out=dst->im,.color_view=color->view,.depth_view=depth->view,.mv_view=motion->view,.out_view=dst->view,.color_format=VK_FORMAT_R16G16B16A16_SFLOAT,.depth_format=VK_FORMAT_R32_SFLOAT,.out_format=VK_FORMAT_R16G16B16A16_SFLOAT,.w=w,.h=h,.ow=tw,.oh=thh,.jx=-pc.j[0],.jy=-pc.j[1],.dt_ms=fminf(100,fmaxf(1,ms)),.near_z=1.f/fmaxf(.001f,cur[11]+cur[15]),.far_z=1.f/fmaxf(.000001f,cur[15]),.fov_y=2*atanf(sqrtf(cur[4]*cur[4]+cur[5]*cur[5]+cur[6]*cur[6])),.reset=!pc.valid||ms>200};
 bool material_history=false;
 FlowParams p={0};
 memcpy(p.clipToPrevious,pc.m,sizeof pc.m);memcpy(p.jitter,pc.j,sizeof pc.j);
 memcpy(p.previousJitter,material_last_jitter,sizeof p.previousJitter);
 if(flow_set){
  p.historyValid=material_flow.warm && !f.reset && material_last_frame+1==g_vk_present && material_last_address==cap_material.address;
  material_history=p.historyValid;
  FlowInputs inputs={color->view,depth->view,material_view,motion->view,vendor_layout,vendor_layout,ro};
  flow_record(&material_flow,cb,flow_set,&inputs,p);
  if(!dl && material_history){
   flow_mask_layout(&material_flow,cb,ro);
   f.reactive=material_flow.images[FlowMask].image;f.reactive_view=material_flow.images[FlowMask].view;
  }
  material_last_frame=g_vk_present;material_last_address=cap_material.address;
  memcpy(material_last_jitter,pc.j,sizeof pc.j);
 }else material_flow.warm=0;
 if(!dl){flow_image_layout(cb,motion->im,rw,ro);motion->layout=ro;}
 static int init;if(dl&&!init){init=1;vk_dlaa_init(g_vk.instance,g_vk.phys,g_vk.dev);}
 bool ok=dl?temporal_ngx_record(g_vk.dev,cb,&f):vk_fsr3_record(g_vk.phys,g_vk.dev,cb,&f);
 if(ok && dl && f.reset)LOG_LIMIT(8,"arceus-upscaler: DLSS history reinitialized at frame=%u\n",g_vk_present);
 vk_render_forget_state();if(!ok){disabled=1;tready=0;WARN("arceus-upscaler: vendor dispatch failed; reverting to original lighting for session\n");return;}
 temporal_snapshot_set(dl?"DLSS":"FSR",material_history?"material+camera":flow_set?"material-warmup":"camera-only",w,h,
     material?material->gva:0,motion->im,motion->layout,flow_set?&material_flow:NULL,&p);
 tuse(cb,dst,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);up_override=dst->view;up_address=cap_color;
 LOG_LIMIT(32,"arceus-upscaler: %s pre-HUD %ux%u -> %ux%u frame=%u history=%u jitter=%g,%g motion=%s\n",dl?"DLSS":"FSR 3.1.6",w,h,tw,thh,g_vk_present,!f.reset,pc.j[0],pc.j[1],material_history?"material+camera":flow_set?"material-warmup":"camera-only");
 previous_color=cap_color;previous_color_write=col->wseq;previous_depth_write=z->wseq;
 memcpy(tprev,cur,64);tindex^=1;tready=1;tlast=g_vk_present;tzaddr=z->gva;
}

