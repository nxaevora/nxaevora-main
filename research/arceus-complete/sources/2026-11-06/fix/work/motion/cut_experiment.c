/* Replay three captured/composited frames through the same material estimator and
   native resolve or signed upscaler that the test launchers use. */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "../ups/vk_dlaa.h"
#include "material_flow.h"
#include "cut_frame.h"
#include "replay_io.h"
#define OK(call) CHECK(call)
#include "replay_device.h"
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint8_t u8;
#include "../taa/taa_words.h"
/* Standalone equivalents of the renderer's readback/submit hooks. The capture
   function and image/layout selection are the same code used by F3 in game. */
static struct {
    ReplayDevice dev;
    VkCommandBuffer cb;
    HostBuffer staging;
    size_t capacity;
} readback;
static u32 g_vk_present;
static VkCommandBuffer vk_cmd(void){return readback.cb;}
static void vk_render_end(void){}
static void vk_rec_sync(void){}
static void vk_readback_ready(void){}
static u8 *vk_readback_buffer(u64 size,VkBuffer *buffer){
    if(size>readback.capacity){
        if(readback.staging.buffer)destroy_buffer(readback.dev.device,readback.staging);
        readback.staging=host_buffer(readback.dev.physical,readback.dev.device,size,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        readback.capacity=(size_t)size;
    }
    *buffer=readback.staging.buffer;
    CHECK(vkResetCommandBuffer(readback.cb,0));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(readback.cb,&begin));return readback.staging.bytes;
}
static void vk_finish(void){
    CHECK(vkEndCommandBuffer(readback.cb));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&readback.cb};
    CHECK(vkQueueSubmit(readback.dev.queue,1,&submit,VK_NULL_HANDLE));CHECK(vkQueueWaitIdle(readback.dev.queue));
}
#define LOG printf
#include "capture.inc"

void vk_fsr_log(const char *fmt,...) {
    va_list args;va_start(args,fmt);vprintf(fmt,args);va_end(args);
}
void vk_fsr_message(const char *tag,bool error,const wchar_t *message) {
    printf("%s (%d) %ls\n",tag,error,message);
}
typedef struct {float matrix[16],jitter[2],weight,epsilon;uint32_t valid,motion,reactive;} ResolveParams;
typedef struct {VkDescriptorSetLayout set;VkPipelineLayout layout;VkPipeline pipe;} Resolve;
static Resolve native_pipeline(VkDevice device) {
    Resolve out={0};VkDescriptorSetLayoutBinding b[8]={0};
    for(int i=0;i<8;i++)b[i]=(VkDescriptorSetLayoutBinding){i,i<6?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo sl={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=8,.pBindings=b};
    CHECK(vkCreateDescriptorSetLayout(device,&sl,NULL,&out.set));
    VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(ResolveParams)};
    VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&out.set,.pushConstantRangeCount=1,.pPushConstantRanges=&range};
    CHECK(vkCreatePipelineLayout(device,&pl,NULL,&out.layout));
    VkShaderModuleCreateInfo sh={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof taa_words,.pCode=taa_words};
    VkShaderModule module;CHECK(vkCreateShaderModule(device,&sh,NULL,&module));
    VkComputePipelineCreateInfo pipe={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=out.layout};
    CHECK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipe,NULL,&out.pipe));
    vkDestroyShaderModule(device,module,NULL);return out;
}
static VkDescriptorSet alloc_set(VkDevice dev,VkDescriptorPool pool,VkDescriptorSetLayout layout){
    VkDescriptorSetAllocateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&layout};
    VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(dev,&di,&set));return set;
}
static void record_native(VkDevice dev,VkCommandBuffer cb,Resolve r,VkDescriptorSet set,VkSampler sampler,
                          const VkImageView views[8],const VkImageLayout layouts[8],ResolveParams params,u32 w,u32 h){
    VkDescriptorImageInfo info[8];VkWriteDescriptorSet writes[8];
    for(int i=0;i<8;i++){
        info[i]=(VkDescriptorImageInfo){i<6?sampler:VK_NULL_HANDLE,views[i],layouts[i]};
        writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,.descriptorCount=1,
            .descriptorType=i<6?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,.pImageInfo=&info[i]};
    }
    vkUpdateDescriptorSets(dev,8,writes,0,NULL);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,r.pipe);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,r.layout,0,1,&set,0,NULL);
    vkCmdPushConstants(cb,r.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof params,&params);
    vkCmdDispatch(cb,(w+7)/8,(h+7)/8,1);
}

int main(int argc,char **argv){
    if(argc!=6){fprintf(stderr,"TemporalReplay BACKEND MODE WIDTH HEIGHT sequence.txt\n");return 1;}
    bool dl=!strcmp(argv[1],"DLSS"),native=!strcmp(argv[1],"TAA");
    bool estimate=!strcmp(argv[2],"estimate") || !strcmp(argv[2],"mask");
    bool mask=!strcmp(argv[2],"mask");
    if(!dl && !native && strcmp(argv[1],"FSR"))return 1;
    if(!estimate && strcmp(argv[2],"supplied"))return 1;
    u32 w=(u32)atoi(argv[3]),h=(u32)atoi(argv[4]),ow=native?w:2560,oh=native?h:1440;
    FILE *sequence=fopen(argv[5],"rt");if(!sequence)return 3;
    _putenv_s("ARCEUS_UPSCALER",argv[1]);
    ReplayDevice d=open_device(dl);VkDevice dev=d.device;
    FlowGpu flow={0};CHECK(flow_create(&flow,d.physical,dev,w,h,VK_NULL_HANDLE));
    VkImageUsageFlags usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if(native && estimate)CHECK(flow_make_image(&flow,d.physical,&flow.nativeMotion,w,h,VK_FORMAT_R16G16_SFLOAT,usage));
    enum {Color,Depth,Material,Motion,Output,HistoryColorA,HistoryDepthA,HistoryColorB,HistoryDepthB,Count};
    const VkFormat formats[]={VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R32_SFLOAT,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R32_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R32_SFLOAT};
    FlowImage image[Count]={0};VkImageLayout layout[Count]={0};
    const VkImageLayout general=VK_IMAGE_LAYOUT_GENERAL,ro=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    for(int i=0;i<(native?Count:HistoryColorA);i++)CHECK(flow_make_image(&flow,d.physical,&image[i],i==Output?ow:w,i==Output?oh:h,formats[i],usage));
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,12},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,10}};
    VkDescriptorPoolCreateInfo dp={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=2,.poolSizeCount=2,.pPoolSizes=sizes};
    VkDescriptorPool desc;CHECK(vkCreateDescriptorPool(dev,&dp,NULL,&desc));
    VkDescriptorSet flowSet=alloc_set(dev,desc,flow.setLayout),nativeSet=VK_NULL_HANDLE;
    Resolve r={0};if(native){r=native_pipeline(dev);nativeSet=alloc_set(dev,desc,r.set);}
    VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=d.family,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    VkCommandPool pool;CHECK(vkCreateCommandPool(dev,&cp,NULL,&pool));
    VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cb;CHECK(vkAllocateCommandBuffers(dev,&ai,&cb));
    readback.dev=d;readback.cb=cb;
    const size_t pixels=(size_t)w*h,outputBytes=(size_t)ow*oh*8;
    HostBuffer upload=host_buffer(d.physical,dev,pixels*20,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    HostBuffer download=host_buffer(d.physical,dev,outputBytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    const char *suffix[]={"color","depth","material","flow"};
    size_t offset[]={0,8*pixels,12*pixels,16*pixels},length[]={8*pixels,4*pixels,4*pixels,4*pixels};
    char source[180],motionSource[180],out[180];float dx,dy,jx,jy,previousJitter[2]={0};int valid,tick=0;
    while(fscanf(sequence,"%179s %179s %179s %f %f %f %f %d",source,motionSource,out,&dx,&dy,&jx,&jy,&valid)==8){
        for(int i=0;i<4;i++)image_file(i==Motion?motionSource:source,suffix[i],upload.bytes+offset[i],length[i],0);
        CHECK(vkResetCommandBuffer(cb,0));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cb,&begin));
        for(int i=0;i<(native?Count:HistoryColorA);i++){
            flow_image_layout(cb,image[i].image,layout[i],general);layout[i]=general;
            if(i>Motion)continue;
            VkBufferImageCopy copy={.bufferOffset=offset[i],.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={w,h,1}};
            vkCmdCopyBufferToImage(cb,upload.buffer,image[i].image,general,1,&copy);
        }
        VkImageLayout vendorLayout=dl?general:ro;
        for(int i=Color;i<=Material;i++){
            layout[i]=i==Material?ro:vendorLayout;
            flow_image_layout(cb,image[i].image,general,layout[i]);
        }
        flow_memory(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        FlowParams p={.jitter={jx,jy},.previousJitter={previousJitter[0],previousJitter[1]},.historyValid=valid && tick};
        for(int j=0;j<4;j++)p.clipToPrevious[5*j]=1;
        p.clipToPrevious[12]=2*dx/w;p.clipToPrevious[13]=-2*dy/h;
        FlowImage mv=(native&&estimate)?flow.nativeMotion:image[Motion];
        if(estimate){
            FlowInputs in={image[Color].view,image[Depth].view,image[Material].view,mv.view,layout[Color],layout[Depth],ro};
            flow_record(&flow,cb,flowSet,&in,p);
        }
        VkImage result=image[Output].image;
        if(native){
            int old=HistoryColorA+2*(tick%2),dst=HistoryColorA+2*(1-tick%2);
            const VkImageView views[]={image[Color].view,image[Depth].view,image[old].view,image[old+1].view,mv.view,mask?flow.images[FlowMask].view:image[Depth].view,image[dst].view,image[dst+1].view};
            const VkImageLayout layouts[]={ro,ro,general,general,general,mask?general:ro,general,general};
            ResolveParams rp={.weight=.72f,.epsilon=.01f,.valid=p.historyValid,.motion=2,.reactive=mask};
            memcpy(rp.matrix,p.clipToPrevious,sizeof rp.matrix);memcpy(rp.jitter,p.jitter,sizeof rp.jitter);
            record_native(dev,cb,r,nativeSet,flow.sampler,views,layouts,rp,w,h);result=image[dst].image;
        }else{
            layout[Motion]=vendorLayout;flow_image_layout(cb,mv.image,general,vendorLayout);
            VkFsr2Frame f={.color=image[Color].image,.depth=image[Depth].image,.mv=mv.image,.out=result,
                .color_view=image[Color].view,.depth_view=image[Depth].view,.mv_view=mv.view,.out_view=image[Output].view,
                .color_format=formats[Color],.depth_format=formats[Depth],.out_format=formats[Output],
                .w=w,.h=h,.ow=ow,.oh=oh,.jx=-jx,.jy=-jy,.dt_ms=8.33f,.near_z=.1f,.far_z=5000,.fov_y=1.f,.reset=!p.historyValid};
            if(mask){flow_mask_layout(&flow,cb,vendorLayout);f.reactive=flow.images[FlowMask].image;f.reactive_view=flow.images[FlowMask].view;}
            if(!(dl?vk_dlaa_record(dev,cb,&f):vk_fsr3_record(d.physical,dev,cb,&f)))return 4;
            if(dl && f.reset){ flow_memory(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT); if(!vk_dlaa_record(dev,cb,&f))return 4; }
        }
        bool save=strcmp(out,"-")!=0;
        if(save){
            flow_memory(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
            VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={ow,oh,1}};
            vkCmdCopyImageToBuffer(cb,result,general,download.buffer,1,&copy);
            flow_memory(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
        }
        CHECK(vkEndCommandBuffer(cb));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};
        CHECK(vkQueueSubmit(d.queue,1,&submit,VK_NULL_HANDLE));CHECK(vkQueueWaitIdle(d.queue));
        if(save){
            image_file(out,"raw",download.bytes,outputBytes,1);
            if(getenv("ARCEUS_REPLAY_SNAPSHOT")){
                g_vk_present=(u32)tick;
                temporal_snapshot_set(argv[1],estimate?"material+camera":"supplied",w,h,0,mv.image,native?general:vendorLayout,estimate?&flow:NULL,&p);
                temporal_snapshot_capture(out);
            }
        }
        previousJitter[0]=jx;previousJitter[1]=jy;tick++;
    }
    fclose(sequence);printf("Replayed %d frames.\n",tick);return tick?0:3;
}
