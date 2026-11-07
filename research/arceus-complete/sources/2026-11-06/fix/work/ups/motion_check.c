#include "vk_dlaa.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
void vk_fsr_log(const char *f,...){va_list a;va_start(a,f);vprintf(f,a);va_end(a);fflush(stdout);}
void vk_fsr_message(const char*t,bool e,const wchar_t*m){printf("%s %d %ls\n",t,e,m);}
#define OK(x) do{VkResult err=(x);if(err){printf("Vulkan %s: %d\n",#x,err);return 2;}}while(0)
static VkPhysicalDeviceMemoryProperties mem;
static unsigned mt(unsigned mask,unsigned needed){for(unsigned i=0;i<mem.memoryTypeCount;i++)if((mask>>i&1)&&(mem.memoryTypes[i].propertyFlags&needed)==needed)return i;exit(5);}
int main(int argc,char**argv){if(argc!=2 && argc!=5)return 1;bool replay=argc==5;bool dl=!strcmp(argv[1],"DLSS");_putenv_s("ARCEUS_UPSCALER",argv[1]);const char *ext[160];unsigned n=0;if(dl)vk_dlaa_instance_exts(ext,&n,160);
VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="ArceusUpscaleOfflineTest",.apiVersion=VK_API_VERSION_1_3};VkInstanceCreateInfo ic={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.enabledExtensionCount=n,.ppEnabledExtensionNames=ext};VkInstance inst;OK(vkCreateInstance(&ic,0,&inst));unsigned count=1;VkPhysicalDevice phys;VkResult rr=vkEnumeratePhysicalDevices(inst,&count,&phys);if(rr<0||!count)return 2;
VkPhysicalDeviceProperties prop;vkGetPhysicalDeviceProperties(phys,&prop);printf("%s\n",prop.deviceName);
uint32_t nq=0;vkGetPhysicalDeviceQueueFamilyProperties(phys,&nq,0);VkQueueFamilyProperties qp[64];if(nq>64)return 2;vkGetPhysicalDeviceQueueFamilyProperties(phys,&nq,qp);unsigned fam=0;while(fam<nq && (qp[fam].queueFlags&3)!=3)fam++;if(fam==nq)return 2;
float one=1;VkDeviceQueueCreateInfo qc={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=fam,.queueCount=1,.pQueuePriorities=&one};vkEnumerateDeviceExtensionProperties(phys,0,&count,0);VkExtensionProperties *exts=calloc(count,sizeof *exts);vkEnumerateDeviceExtensionProperties(phys,0,&count,exts);n=0;if(dl)vk_dlaa_device_exts(exts,count,ext,&n,160);
VkPhysicalDeviceVulkan13Features f13={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};VkPhysicalDeviceVulkan12Features f12={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,.pNext=&f13};VkPhysicalDeviceVulkan11Features f11={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,.pNext=&f12};VkPhysicalDeviceFeatures2 f2={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&f11};vkGetPhysicalDeviceFeatures2(phys,&f2);
VkPhysicalDeviceVulkan11Features e11={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,.shaderDrawParameters=f11.shaderDrawParameters,.samplerYcbcrConversion=f11.samplerYcbcrConversion};
VkPhysicalDeviceVulkan12Features e12={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,.pNext=&e11,.samplerMirrorClampToEdge=f12.samplerMirrorClampToEdge,.samplerFilterMinmax=f12.samplerFilterMinmax,.timelineSemaphore=1,.shaderFloat16=f12.shaderFloat16,.shaderBufferInt64Atomics=f12.shaderBufferInt64Atomics,.shaderOutputViewportIndex=f12.shaderOutputViewportIndex,.shaderOutputLayer=f12.shaderOutputLayer,.shaderSampledImageArrayNonUniformIndexing=f12.shaderSampledImageArrayNonUniformIndexing};
VkPhysicalDeviceVulkan13Features e13={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,.pNext=&e12,.synchronization2=1,.dynamicRendering=1,.shaderDemoteToHelperInvocation=1};f2.pNext=&e13;
VkDeviceCreateInfo dc={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&f2,.queueCreateInfoCount=1,.pQueueCreateInfos=&qc,.enabledExtensionCount=n,.ppEnabledExtensionNames=ext};VkDevice dev;OK(vkCreateDevice(phys,&dc,0,&dev));if(dl){vk_dlaa_init(inst,phys,dev);if(!vk_dlaa_available())return 3;}
vkGetPhysicalDeviceMemoryProperties(phys,&mem);VkQueue q;vkGetDeviceQueue(dev,fam,0,&q);
VkCommandPool pool;VkCommandPoolCreateInfo cp={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=fam};OK(vkCreateCommandPool(dev,&cp,0,&pool));VkCommandBuffer cmd;VkCommandBufferAllocateInfo ca={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.commandBufferCount=1};OK(vkAllocateCommandBuffers(dev,&ca,&cmd));VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};OK(vkBeginCommandBuffer(cmd,&bi));
VkFsr2Frame frame={.w=1920,.h=1080,.ow=2560,.oh=1440,.reset=true,.dt_ms=8.33,.near_z=.1f,.far_z=5000,.fov_y=1.f,.out_format=VK_FORMAT_R16G16B16A16_SFLOAT,.color_format=VK_FORMAT_R16G16B16A16_SFLOAT,.depth_format=VK_FORMAT_R32_SFLOAT};
VkImage imgs[4];VkImageView views[4];VkImageSubresourceRange sub={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
for(int i=0;i<4;i++){VkFormat fmt=i==0||i==3?frame.color_format:i==1?frame.depth_format:VK_FORMAT_R16G16_SFLOAT;
VkImageCreateInfo im={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=fmt,.extent={i==3?frame.ow:frame.w,i==3?frame.oh:frame.h,1},.mipLevels=1,.arrayLayers=1,.samples=1,.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};OK(vkCreateImage(dev,&im,0,&imgs[i]));VkMemoryRequirements r;vkGetImageMemoryRequirements(dev,imgs[i],&r);VkDeviceMemory m;VkMemoryAllocateInfo ma={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=r.size,.memoryTypeIndex=mt(r.memoryTypeBits,0)};OK(vkAllocateMemory(dev,&ma,0,&m));OK(vkBindImageMemory(dev,imgs[i],m,0));VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=imgs[i],.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=fmt,.subresourceRange=sub};OK(vkCreateImageView(dev,&vi,0,&views[i]));
VkImageMemoryBarrier ib={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.newLayout=VK_IMAGE_LAYOUT_GENERAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=imgs[i],.subresourceRange=sub};vkCmdPipelineBarrier(cmd,1,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,0,0,0,1,&ib);VkClearColorValue c={{i==0?.25f:i==1?.5f:0,i==0?.5f:0,i==0?.75f:0,1}};vkCmdClearColorImage(cmd,imgs[i],VK_IMAGE_LAYOUT_GENERAL,&c,1,&sub);}
VkMemoryBarrier mb={VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT};vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&mb,0,0,0,0);
frame.color=imgs[0];frame.depth=imgs[1];frame.mv=imgs[2];frame.out=imgs[3];frame.color_view=views[0];frame.depth_view=views[1];frame.mv_view=views[2];frame.out_view=views[3];

const VkImageLayout readLayout=dl?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
for(int tick=0;tick<3;tick++){
 const bool changed=tick==2;
 const bool cold=!strcmp(argv[2],"cold");
 const char*prefix=(changed||cold)?argv[3]:"original";
 frame.reset=tick==0||(changed&&!strcmp(argv[2],"reset"))||cold;
 for(int i=0;i<3;i++){
    VkImageMemoryBarrier writable={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
       .srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
       .oldLayout=tick?readLayout:VK_IMAGE_LAYOUT_GENERAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
       .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
       .image=imgs[i],.subresourceRange=sub};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,0,0,0,1,&writable);
 }
for(int i=0;i<3;i++){
 VkDeviceSize sz=(VkDeviceSize)frame.w*frame.h*(i?4:8);VkBuffer ub;VkBufferCreateInfo ci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=sz,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT};OK(vkCreateBuffer(dev,&ci,0,&ub));VkMemoryRequirements r;vkGetBufferMemoryRequirements(dev,ub,&r);VkDeviceMemory mm;VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=r.size,.memoryTypeIndex=mt(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};OK(vkAllocateMemory(dev,&ai,0,&mm));OK(vkBindBufferMemory(dev,ub,mm,0));void*ptr;OK(vkMapMemory(dev,mm,0,sz,0,&ptr));char file[128]; snprintf(file,sizeof file,"%s-%d.bin",prefix,i);FILE*fi=fopen(file,"rb");if(!fi||fread(ptr,1,sz,fi)!=sz)return 6;fclose(fi);vkUnmapMemory(dev,mm);
 VkMemoryBarrier wb={VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&wb,0,0,0,0);
 VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={frame.w,frame.h,1}};vkCmdCopyBufferToImage(cmd,ub,imgs[i],VK_IMAGE_LAYOUT_GENERAL,1,&copy);
 }
for(int i=0;i<3;i++){VkImageMemoryBarrier ib={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT,.oldLayout=VK_IMAGE_LAYOUT_GENERAL,.newLayout=dl?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=imgs[i],.subresourceRange=sub};vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,0,0,0,1,&ib);}
if(!(dl?vk_dlaa_record(dev,cmd,&frame):vk_fsr3_record(phys,dev,cmd,&frame)))return 4; mb.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&mb,0,0,0,0);}
VkDeviceSize nbytes=replay?(VkDeviceSize)frame.ow*frame.oh*8:8;VkBuffer buf;VkBufferCreateInfo bc={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=nbytes,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};OK(vkCreateBuffer(dev,&bc,0,&buf));VkMemoryRequirements r;vkGetBufferMemoryRequirements(dev,buf,&r);VkDeviceMemory m;VkMemoryAllocateInfo ma={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=r.size,.memoryTypeIndex=mt(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};OK(vkAllocateMemory(dev,&ma,0,&m));OK(vkBindBufferMemory(dev,buf,m,0));mb.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&mb,0,0,0,0);
VkBufferImageCopy cpix={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageOffset={1280,720,0},.imageExtent={1,1,1}};if(replay){cpix.imageOffset=(VkOffset3D){0};cpix.imageExtent=(VkExtent3D){frame.ow,frame.oh,1};}vkCmdCopyImageToBuffer(cmd,imgs[3],VK_IMAGE_LAYOUT_GENERAL,buf,1,&cpix);mb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,0,0,0);OK(vkEndCommandBuffer(cmd));VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};OK(vkQueueSubmit(q,1,&si,0));OK(vkQueueWaitIdle(q));_Float16 *mapped;OK(vkMapMemory(dev,m,0,nbytes,0,(void**)&mapped));if(replay){FILE*fo=fopen(argv[4],"wb");if(!fo||fwrite(mapped,1,nbytes,fo)!=nbytes)return 6;fclose(fo);printf("Captured lighting replay completed.\n");return 0;}printf("1920x1080 -> 2560x1440, output RGB %.4f %.4f %.4f\n",(double)mapped[0],(double)mapped[1],(double)mapped[2]);return !(fabsf(mapped[0]-.25f)<.035f && fabsf(mapped[1]-.5f)<.035f && fabsf(mapped[2]-.75f)<.035f);}


