#include <math.h>
#include "taa_words.h"
#include "../motion/material_flow.h"
#include "nx/runtime/platform/plat.h"
#include "../motion/capture.inc"
typedef struct {VkImage im; VmaAllocation mem; VkImageView view; VkImageLayout layout;} THist;
static THist th[2][2];
static VkDescriptorSetLayout tdsl; static VkPipelineLayout tpl; static VkPipeline tpipe; static VkSampler tsamp;
static u32 tlast,tw,thh,tindex,tjitframe; static int tready,tjitvalid;
static float tprev[16]; static u64 tzaddr;
static void tjitter(float *xy){ static const float a[8][2]={{0,-.166667f},{-.25f,.166667f},{.25f,-.388889f},{-.375f,-.055556f},{.125f,.277778f},{-.125f,-.277778f},{.375f,.055556f},{-.4375f,.388889f}};memcpy(xy,a[g_vk_present%8],8);}
static int tinit(void){
 static int attempted;if(attempted)return tpipe!=0;attempted=1;
 VkFormatProperties a,bfmt;vkGetPhysicalDeviceFormatProperties(g_vk.phys,VK_FORMAT_R16G16B16A16_SFLOAT,&a);vkGetPhysicalDeviceFormatProperties(g_vk.phys,VK_FORMAT_B10G11R11_UFLOAT_PACK32,&bfmt);if(!(a.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)||!(bfmt.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))return 0;
 VkDescriptorSetLayoutBinding b[8];for(int i=0;i<8;i++) b[i]=(VkDescriptorSetLayoutBinding){i,i<6?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
 VkDescriptorSetLayoutCreateInfo dc={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=8,.pBindings=b};
 if(vkCreateDescriptorSetLayout(g_vk.dev,&dc,0,&tdsl))return 0;
 VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,92};
 VkPipelineLayoutCreateInfo lc={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&tdsl,.pushConstantRangeCount=1,.pPushConstantRanges=&range};
 if(vkCreatePipelineLayout(g_vk.dev,&lc,0,&tpl))return 0;
 VkSamplerCreateInfo sc={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
 if(vkCreateSampler(g_vk.dev,&sc,0,&tsamp))return 0;
 VkShaderModule m=vk_module_spv(taa_words,sizeof taa_words/4);if(!m)return 0;
 VkComputePipelineCreateInfo pc={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=m,.pName="main"},.layout=tpl};
 VkResult r=vkCreateComputePipelines(g_vk.dev,vk_pipeline_cache(),1,&pc,0,&tpipe);vkDestroyShaderModule(g_vk.dev,m,0);return !r;
}
static int timages(u32 w,u32 h){
 if(tw==w && thh==h)return 1;
 tw=thh=0;tready=0;temporal_snapshot.backend=NULL;
 for(int i=0;i<4;i++){THist *t=&th[i/2][i%2];if(t->im)vk_defer_image(t->im,t->mem,t->view,0);memset(t,0,sizeof *t);}
 for(int i=0;i<4;i++){THist *t=&th[i/2][i%2];VkFormat f=i%2?VK_FORMAT_R32_SFLOAT:VK_FORMAT_R16G16B16A16_SFLOAT;
 VkImageCreateInfo c={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=f,.extent={w,h,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
 if(!vk_image_create(&c,&t->im,&t->mem))return 0;
 VkImageViewCreateInfo v={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=t->im,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=f,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
 if(vkCreateImageView(g_vk.dev,&v,0,&t->view))return 0;}
 tw=w;thh=h;return 1;
}
#include "../taa/temporal_camera.h"
static void tuse(VkCommandBuffer cb,THist *t,VkImageLayout l){
 vk_barrier(cb,t->im,VK_IMAGE_ASPECT_COLOR_BIT,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,VK_ACCESS_2_MEMORY_WRITE_BIT|VK_ACCESS_2_MEMORY_READ_BIT,t->layout,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT,l);t->layout=l;
}
static FlowGpu native_flow;
static int native_flow_attempted;
static u64 native_material_address,native_color_address,native_time;
static float native_previous_jitter[2];
static VkDescriptorSet native_motion_set(Surf *material,u32 w,u32 h){
 if(!material || material->layers!=1 || material->sw!=w || material->sh!=h ||
    material->f.vk!=VK_FORMAT_R8G8B8A8_UNORM || material->layout==VK_IMAGE_LAYOUT_UNDEFINED)return VK_NULL_HANDLE;
 if(w!=2560 || h!=1440)return VK_NULL_HANDLE;
 if(!native_flow_attempted){
  native_flow_attempted=1;
  const char *setting=getenv("SNX_ARCEUS_MATERIAL_MOTION");
  if(!setting || strcmp(setting,"0")){
   VkResult result=flow_create(&native_flow,g_vk.phys,g_vk.dev,w,h,vk_pipeline_cache());
   if(result==VK_SUCCESS)result=flow_make_image(&native_flow,g_vk.phys,&native_flow.nativeMotion,w,h,
         VK_FORMAT_R16G16_SFLOAT,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
   if(result!=VK_SUCCESS)flow_destroy(&native_flow);
   LOG("arceus-motion: native material estimator initialization %d at %ux%u\n",result,w,h);
  }
 }
 return native_flow.nativeMotion.view?vk_desc_alloc(native_flow.setLayout):VK_NULL_HANDLE;
}
static void taa_after(GpuChannel *ch,VkSh *fs,TexRes *tex,Surf *col){
 if(!fs||!col||(fs->hash!=0x89619027ba43e5d3ull && fs->hash!=0xe02c1a109a77fac4ull)||col->f.vk!=VK_FORMAT_B10G11R11_UFLOAT_PACK32||col->layers!=1||ch->render_pred)return;
 if(tready && tlast==g_vk_present)return;
 Surf *z=NULL,*material=NULL;
 for(int i=0;i<fs->si.ntex&&i<SH_MAXTEX;i++)if(tex[i].have){
  if(fs->si.tex[i].cbuf_off==0x32)z=tex[i].t;
  if(fs->si.tex[i].cbuf_off==0x2e)material=tex[i].t;
 }
 LOG_LIMIT(4,"arceus-taa: matched lighting target %ux%u depth=%llx\n",col->sw,col->sh,(unsigned long long)(z?z->gva:0));
 if(!z||z->f.vk!=VK_FORMAT_D24_UNORM_S8_UINT||z->sw!=col->sw||z->sh!=col->sh||!z->sw||!z->sh)return;
 u64 addr=ch->cbind[4][4];VkBuffer b;u64 off;if(!addr||cb_range(ch,4,fs,4)<256||vk_buf_pending(addr,256)||vk_compute_gpu_src(addr,256,&b,&off))return;
 const void *m=gmem_ptr(addr+192,64);if(!m)return;float cur[16];memcpy(cur,m,64);
 VkComponentMapping id={0};VkImageView cv=vk_tex_view(col,VK_IMAGE_VIEW_TYPE_2D,0,&id,0),zv=vk_tex_view(z,VK_IMAGE_VIEW_TYPE_2D,0,&id,0);
 if(!cv||!zv||!tinit()||!timages(col->sw,col->sh)){LOG_LIMIT(2,"arceus-taa: resources unavailable\n");return;}
 VkDescriptorSet set=vk_desc_alloc(tdsl);if(!set)return;
 VkCommandBuffer cb=vk_cmd();vk_render_end();
 const VkImageLayout ro=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,rw=VK_IMAGE_LAYOUT_GENERAL;
 vk_surf_use(cb,col,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
 vk_surf_use(cb,z,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
 THist *old=th[tindex],*dst=th[tindex^1];for(int i=0;i<2;i++){tuse(cb,old+i,ro);tuse(cb,dst+i,rw);}
 struct {float m[16],j[2],w,eps;u32 valid,mv,react;} pc={.w=.72f,.eps=.01f};
 if(tjitvalid && tjitframe==g_vk_present)tjitter(pc.j);
 u64 now=plat_ns();
 pc.valid=tready && tlast+1==g_vk_present && tzaddr==z->gva && native_color_address==col->gva &&
          now-native_time<=200000000ull && tmatrix(cur,tprev,pc.m);
 VkImageView views[]={cv,zv,old[0].view,old[1].view,zv,zv,dst[0].view,dst[1].view};VkDescriptorImageInfo ii[8];VkWriteDescriptorSet wr[8];
 VkDescriptorSet flow_set=native_motion_set(material,tw,thh);
 VkImageView material_view=flow_set?vk_tex_view(material,VK_IMAGE_VIEW_TYPE_2D,0,&id,0):VK_NULL_HANDLE;
 FlowParams p={0};memcpy(p.clipToPrevious,pc.m,sizeof pc.m);memcpy(p.jitter,pc.j,sizeof pc.j);
 memcpy(p.previousJitter,native_previous_jitter,sizeof p.previousJitter);
 if(material_view){
  vk_surf_touch(material);
  vk_surf_use(cb,material,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  vk_rec_sync();
  p.historyValid=pc.valid && native_flow.warm && native_material_address==material->gva;
  FlowInputs inputs={cv,zv,material_view,native_flow.nativeMotion.view,ro,ro,ro};
  flow_record(&native_flow,cb,flow_set,&inputs,p);
  if(p.historyValid){pc.mv=2;pc.react=1;views[4]=native_flow.nativeMotion.view;views[5]=native_flow.images[FlowMask].view;}
  memcpy(native_previous_jitter,pc.j,sizeof pc.j);native_material_address=material->gva;
 }else native_flow.warm=0;
 for(int i=0;i<8;i++){
  VkImageLayout layout=i>=6 || (pc.mv==2 && (i==4 || i==5))?rw:ro;
  ii[i]=(VkDescriptorImageInfo){i<6?tsamp:0,views[i],layout};
  wr[i]=(VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=i<6?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,.pImageInfo=ii+i};
 }
 vkUpdateDescriptorSets(g_vk.dev,8,wr,0,NULL);
 native_time=now;native_color_address=col->gva;
 temporal_snapshot_set("TAA",pc.mv==2?"material+camera":material_view?"material-warmup":"camera-only",tw,thh,
     material?material->gva:0,material_view?native_flow.nativeMotion.image:VK_NULL_HANDLE,rw,material_view?&native_flow:NULL,&p);
 rec_CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tpipe);rec_CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tpl,0,1,&set,0,NULL);
 rec_CmdPushConstants(cb,tpl,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof pc,&pc);rec_CmdDispatch(cb,(tw+7)/8,(thh+7)/8,1);
 tuse(cb,dst,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);vk_surf_use(cb,col,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT);
 VkImageBlit bl={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}};bl.srcOffsets[1]=bl.dstOffsets[1]=(VkOffset3D){tw,thh,1};
 rec_CmdBlitImage(cb,dst->im,dst->layout,col->img,col->layout,1,&bl,VK_FILTER_NEAREST);target_written(col);vk_render_forget_state();
 LOG_LIMIT(32,"arceus-taa: pre-HUD %ux%u frame=%u history=%u jitter=%g,%g motion=%s\n",tw,thh,g_vk_present,pc.valid,pc.j[0],pc.j[1],pc.mv==2?"material+camera":material_view?"material-warmup":"camera-only");
 memcpy(tprev,cur,64);tindex^=1;tready=1;tlast=g_vk_present;tzaddr=z->gva;
}

