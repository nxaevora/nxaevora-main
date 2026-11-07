from pathlib import Path
p=Path(__file__).resolve().parent;t=p.parent/'taa';s=(t/'integration.inc').read_text()
s=s.replace('#include "taa_words.h"','#include "up_words.h"\n#include "vk_dlaa.h"\n#include "nx/runtime/platform/plat.h"\n#include <stdarg.h>\nvoid vk_fsr_log(const char *fmt,...){char buf[1024];va_list a;va_start(a,fmt);vsnprintf(buf,sizeof buf,fmt,a);va_end(a);LOG("%s",buf);}\nvoid vk_fsr_message(const char*t,bool error,const wchar_t*m){LOG_LIMIT(16,"%s error=%d: %ls\\n",t,error,m);}')
s=s.replace('tpipe!=0','tpipe!=0').replace('b[8];for(int i=0;i<8;i++)','b[5];for(int i=0;i<5;i++)').replace('i<6?VK_DESCRIPTOR_TYPE','(i==0||i==2)?VK_DESCRIPTOR_TYPE').replace('.bindingCount=8','.bindingCount=5').replace('0,92','0,76').replace('taa_words','up_words')
s=s.replace('VkFormat f=i%2?VK_FORMAT_R32_SFLOAT:VK_FORMAT_R16G16B16A16_SFLOAT;', 'VkFormat f=i==1?VK_FORMAT_R32_SFLOAT:i==2?VK_FORMAT_R16G16_SFLOAT:VK_FORMAT_R16G16B16A16_SFLOAT;')
s=s.replace('.extent={w,h,1}', '.extent={i==3?w:(w*2+1)/3,i==3?h:(h*2+1)/3,1}')
s=s.replace('memcpy(xy,a[g_vk_present%8],8);','memcpy(xy,a[g_vk_present%8],8);xy[0]*=1.5f;xy[1]*=1.5f;')
s=s.replace('if(tready && tlast==g_vk_present)return;', 'static int disabled;if(disabled||col->sw!=2560||col->sh!=1440||(tready && tlast==g_vk_present))return;')
a=s.index(' THist *old=');b=s.index(' tuse(cb,dst,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)',a)
s=s[:a]+''' THist *color=&th[0][0],*depth=&th[0][1],*motion=&th[1][0],*dst=&th[1][1];
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
 vk_render_forget_state();if(!ok){disabled=1;tready=0;WARN("arceus-upscaler: vendor dispatch failed; reverting to original lighting for session\\n");return;}
''' +s[b:]
s=s.replace('arceus-taa: pre-HUD %ux%u','arceus-upscaler: %s pre-HUD %ux%u -> %ux%u').replace('camera-only\\n",tw','camera-only\\n",dl?"DLSS":"FSR 3.1.6",w,h,tw')
(p/'integration.inc').write_text(s)
