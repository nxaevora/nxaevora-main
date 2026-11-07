#include <math.h>
#include "up_words.h"
#include "vk_dlaa.h"
#include "nx/runtime/platform/plat.h"
#include <stdarg.h>
void vk_fsr_log(const char *fmt,...){char buf[1024];va_list a;va_start(a,fmt);vsnprintf(buf,sizeof buf,fmt,a);va_end(a);LOG("%s",buf);}
void vk_fsr_message(const char*t,bool error,const wchar_t*m){LOG_LIMIT(16,"%s error=%d: %ls\n",t,error,m);}
typedef struct {VkImage im; VmaAllocation mem; VkImageView view; VkImageLayout layout;} THist;
static THist th[2][2];
static VkDescriptorSetLayout tdsl; static VkPipelineLayout tpl; static VkPipeline tpipe; static VkSampler tsamp;
static u32 tlast,tw,thh,tindex,tjitframe; static int tready,tjitvalid;
static float tprev[16]; static u64 tzaddr;
static void tjitter(float *xy){ static const float a[8][2]={{0,-.166667f},{-.25f,.166667f},{.25f,-.388889f},{-.375f,-.055556f},{.125f,.277778f},{-.125f,-.277778f},{.375f,.055556f},{-.4375f,.388889f}};memcpy(xy,a[g_vk_present%8],8);xy[0]*=1.5f;xy[1]*=1.5f;}
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
 tw=thh=0;tready=0;
 for(int i=0;i<4;i++){THist *t=&th[i/2][i%2];if(t->im)vk_defer_image(t->im,t->mem,t->view,0);memset(t,0,sizeof *t);}
 for(int i=0;i<4;i++){THist *t=&th[i/2][i%2];VkFormat f=i==1?VK_FORMAT_R32_SFLOAT:i==2?VK_FORMAT_R16G16_SFLOAT:VK_FORMAT_R16G16B16A16_SFLOAT;
 VkImageCreateInfo c={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=f,.extent={i==3?w:(w*2+1)/3,i==3?h:(h*2+1)/3,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
 if(!vk_image_create(&c,&t->im,&t->mem))return 0;
 VkImageViewCreateInfo v={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=t->im,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=f,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
 if(vkCreateImageView(g_vk.dev,&v,0,&t->view))return 0;}
 tw=w;thh=h;return 1;
}
static int tmatrix(const float *cur,const float *prev,float *out){
 double a[4][8];for(int i=0;i<4;i++)for(int j=0;j<8;j++)a[i][j]=j<4?prev[4*i+j]:(i==j-4);
 for(int j=0;j<4;j++){int p=j;for(int i=j+1;i<4;i++)if(fabs(a[i][j])>fabs(a[p][j]))p=i;
 if(!(fabs(a[p][j])>1e-10))return 0;
 for(int k=0;k<8;k++){double t=a[p][k];a[p][k]=a[j][k];a[j][k]=t;}
 double d=a[j][j];for(int k=0;k<8;k++)a[j][k]/=d;
 for(int i=0;i<4;i++)if(i!=j){d=a[i][j];for(int k=0;k<8;k++)a[i][k]-=d*a[j][k];}}
 for(int i=0;i<4;i++)for(int j=0;j<4;j++){double s=0;for(int k=0;k<4;k++)s+=cur[i*4+k]*a[k][j+4];out[i*4+j]=(float)s;if(!isfinite(s)||fabs(s-(i==j))>1.0)return 0;}return 1;
}
static void tuse(VkCommandBuffer cb,THist *t,VkImageLayout l){
 vk_barrier(cb,t->im,VK_IMAGE_ASPECT_COLOR_BIT,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,VK_ACCESS_2_MEMORY_WRITE_BIT|VK_ACCESS_2_MEMORY_READ_BIT,t->layout,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT,l);t->layout=l;
}
static void taa_after(GpuChannel *ch,VkSh *fs,TexRes *tex,Surf *col){
 if(!fs||!col||(fs->hash!=0x89619027ba43e5d3ull && fs->hash!=0xe02c1a109a77fac4ull)||col->f.vk!=VK_FORMAT_B10G11R11_UFLOAT_PACK32||col->layers!=1||ch->render_pred)return;
 static int disabled;if(disabled||col->sw!=2560||col->sh!=1440||(tready && tlast==g_vk_present))return;
 Surf *z=0;for(int i=0;i<fs->si.ntex&&i<SH_MAXTEX;i++)if(fs->si.tex[i].cbuf_off==0x32&&tex[i].have)z=tex[i].t;
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
 THist *color=&th[0][0],*depth=&th[0][1],*motion=&th[1][0],*dst=&th[1][1];
 u32 w=(tw*2+1)/3,h=(thh*2+1)/3;
 for(int i=0;i<4;i++)tuse(cb,&th[i/2][i%2],rw);
 VkImageView views[]={zv,motion->view,cv,color->view,depth->view};VkDescriptorImageInfo ii[5];VkWriteDescriptorSet wr[5];
 for(int i=0;i<5;i++){int sampled=i==0||i==2;ii[i]=(VkDescriptorImageInfo){sampled?tsamp:0,views[i],sampled?ro:rw};wr[i]=(VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=sampled?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,.pImageInfo=ii+i};} vkUpdateDescriptorSets(g_vk.dev,5,wr,0,NULL);
 struct{float m[16],j[2];u32 valid;}pc={0};if(tjitvalid&&tjitframe==g_vk_present)tjitter(pc.j);
 pc.valid=tready && tlast+1==g_vk_present && tzaddr==z->gva && tmatrix(cur,tprev,pc.m);
 rec_CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tpipe);rec_CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tpl,0,1,&set,0,NULL);
 rec_CmdPushConstants(cb,tpl,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof pc,&pc);rec_CmdDispatch(cb,(w+7)/8,(h+7)/8,1);
 for(int i=0;i<3;i++)tuse(cb,&th[i/2][i%2],rw);
 vk_rec_sync(); /* finish all queued records before the vendor directly records to this same command buffer */
 static u64 prevtime;u64 now=plat_ns();float ms=prevtime?(float)(now-prevtime)*1e-6f:8.333f;prevtime=now;
 VkFsr2Frame f={.color=color->im,.depth=depth->im,.mv=motion->im,.out=dst->im,.color_view=color->view,.depth_view=depth->view,.mv_view=motion->view,.out_view=dst->view,.color_format=VK_FORMAT_R16G16B16A16_SFLOAT,.depth_format=VK_FORMAT_R32_SFLOAT,.out_format=VK_FORMAT_R16G16B16A16_SFLOAT,.w=w,.h=h,.ow=tw,.oh=thh,.jx=-pc.j[0]*w/tw,.jy=-pc.j[1]*h/thh,.dt_ms=fminf(100,fmaxf(1,ms)),.near_z=1.f/fmaxf(.001f,cur[11]+cur[15]),.far_z=1.f/fmaxf(.000001f,cur[15]),.fov_y=2*atanf(sqrtf(cur[4]*cur[4]+cur[5]*cur[5]+cur[6]*cur[6])),.reset=!pc.valid||ms>200};
 bool dl=vk_dlaa_wanted();static int init;if(dl&&!init){init=1;vk_dlaa_init(g_vk.instance,g_vk.phys,g_vk.dev);}
 bool ok=dl?vk_dlaa_record(g_vk.dev,cb,&f):vk_fsr3_record(g_vk.phys,g_vk.dev,cb,&f);
 vk_render_forget_state();if(!ok){disabled=1;tready=0;WARN("arceus-upscaler: vendor dispatch failed; reverting to original lighting for session\n");return;}
 tuse(cb,dst,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);vk_surf_use(cb,col,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT);
 VkImageBlit bl={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}};bl.srcOffsets[1]=bl.dstOffsets[1]=(VkOffset3D){tw,thh,1};
 rec_CmdBlitImage(cb,dst->im,dst->layout,col->img,col->layout,1,&bl,VK_FILTER_NEAREST);target_written(col);vk_render_forget_state();
 LOG_LIMIT(32,"arceus-upscaler: %s pre-HUD %ux%u -> %ux%u frame=%u history=%u jitter=%g,%g camera-only\n",dl?"DLSS":"FSR 3.1.6",w,h,tw,thh,g_vk_present,pc.valid,pc.j[0],pc.j[1]);
 memcpy(tprev,cur,64);tindex^=1;tready=1;tlast=g_vk_present;tzaddr=z->gva;
}

