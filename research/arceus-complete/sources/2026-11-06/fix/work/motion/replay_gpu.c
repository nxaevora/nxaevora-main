#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "material_flow.h"

#include "replay_io.h"

int main(int argc,char **argv) {
    if(argc!=4 && (argc!=5 || strcmp(argv[4],"native"))) return 1;
    int native=argc==5;
    uint32_t width=(uint32_t)atoi(argv[1]),height=(uint32_t)atoi(argv[2]);
    FILE *frames=fopen(argv[3],"rt"); if(!frames)return 3;
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="Arceus offline motion replay",.apiVersion=VK_API_VERSION_1_2};
    VkInstanceCreateInfo ic={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance instance;CHECK(vkCreateInstance(&ic,NULL,&instance));
    uint32_t deviceCount=0;CHECK(vkEnumeratePhysicalDevices(instance,&deviceCount,NULL));
    if(!deviceCount)return 2;
    VkPhysicalDevice *physicalDevices=calloc(deviceCount,sizeof *physicalDevices);
    CHECK(vkEnumeratePhysicalDevices(instance,&deviceCount,physicalDevices));VkPhysicalDevice physical=physicalDevices[0];free(physicalDevices);
    VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physical,&props);
    uint32_t queueCount=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&queueCount,NULL);
    VkQueueFamilyProperties *qprops=calloc(queueCount,sizeof *qprops);vkGetPhysicalDeviceQueueFamilyProperties(physical,&queueCount,qprops);
    uint32_t family=0;while(family<queueCount && !(qprops[family].queueFlags&VK_QUEUE_COMPUTE_BIT))++family;
    if(family==queueCount || !qprops[family].timestampValidBits)return 2;
    uint32_t bits=qprops[family].timestampValidBits;free(qprops);
    float priority=1;
    VkDeviceQueueCreateInfo qc={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkPhysicalDeviceFeatures features={.shaderStorageImageExtendedFormats=VK_TRUE};
    VkDeviceCreateInfo dc={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qc,.pEnabledFeatures=&features};
    VkDevice device;CHECK(vkCreateDevice(physical,&dc,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    VkCommandPool pool;CHECK(vkCreateCommandPool(device,&cp,NULL,&pool));
    VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cb;CHECK(vkAllocateCommandBuffers(device,&ai,&cb));
    VkQueryPoolCreateInfo qi={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,.queryType=VK_QUERY_TYPE_TIMESTAMP,.queryCount=2};
    VkQueryPool queries;CHECK(vkCreateQueryPool(device,&qi,NULL,&queries));
    FlowGpu flow={0};CHECK(flow_create(&flow,physical,device,width,height,VK_NULL_HANDLE));
    if(native)CHECK(flow_make_image(&flow,physical,&flow.nativeMotion,width,height,VK_FORMAT_R16G16_SFLOAT,
              VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,6}};
    VkDescriptorPoolCreateInfo dp={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};
    VkDescriptorPool descriptors;CHECK(vkCreateDescriptorPool(device,&dp,NULL,&descriptors));
    VkDescriptorSetAllocateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=descriptors,.descriptorSetCount=1,.pSetLayouts=&flow.setLayout};
    VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&di,&set));

    FlowImage inputs[4]={0};
    VkFormat formats[]={VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R32_SFLOAT,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16_SFLOAT};
    for(int i=0;i<4;++i)CHECK(flow_make_image(&flow,physical,inputs+i,width,height,formats[i],
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT));
    const VkImageLayout general=VK_IMAGE_LAYOUT_GENERAL;
    FlowImage velocity=native?flow.nativeMotion:inputs[3];
    FlowInputs in={inputs[0].view,inputs[1].view,inputs[2].view,velocity.view,general,general,general};
    size_t pixels=(size_t)width*height;
    HostBuffer upload=host_buffer(physical,device,16*pixels,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    HostBuffer readback=host_buffer(physical,device,5*pixels,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    const char *suffixes[]={"color","depth","material"};size_t offsets[]={0,8*pixels,12*pixels},lengths[]={8*pixels,4*pixels,4*pixels};
    const VkImageSubresourceRange colorSub={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    char input[160],output[160],lastInput[160]="";float dx,dy,jx,jy,lastJx=0,lastJy=0;int valid,frame=0;
    printf("device\t%s\n",props.deviceName);
    while(fscanf(frames,"%159s %159s %f %f %f %f %d",input,output,&dx,&dy,&jx,&jy,&valid)==7) {
        int changed=strcmp(input,lastInput)!=0;
        if(changed)for(int i=0;i<3;i++)image_file(input,suffixes[i],upload.bytes+offsets[i],lengths[i],0);
        CHECK(vkResetCommandBuffer(cb,0));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cb,&begin));
        if(!frame)for(int i=0;i<4;++i)flow_image_layout(cb,inputs[i].image,VK_IMAGE_LAYOUT_UNDEFINED,general);
        flow_memory(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        if(changed)for(int i=0;i<3;++i){
            VkBufferImageCopy copy={.bufferOffset=offsets[i],.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={width,height,1}};
            vkCmdCopyBufferToImage(cb,upload.buffer,inputs[i].image,general,1,&copy);
        }
        VkClearColorValue clear={{dx,dy,0,0}};vkCmdClearColorImage(cb,inputs[3].image,general,&clear,1,&colorSub);
        flow_memory(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        /* Exclude input uploads and queue an otherwise empty command buffer
           before the timestamps. A bottom-of-pipe start could overlap work. */
        vkCmdResetQueryPool(cb,queries,0,2);
        CHECK(vkEndCommandBuffer(cb));
        VkSubmitInfo uploadSubmit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};
        CHECK(vkQueueSubmit(queue,1,&uploadSubmit,VK_NULL_HANDLE));CHECK(vkQueueWaitIdle(queue));
        CHECK(vkResetCommandBuffer(cb,0));CHECK(vkBeginCommandBuffer(cb,&begin));
        FlowParams params={.jitter={jx,jy},.previousJitter={lastJx,lastJy},.historyValid=(uint32_t)valid};
        for(int i=0;i<4;++i)params.clipToPrevious[i*5]=1;
        params.clipToPrevious[12]=2*dx/width;params.clipToPrevious[13]=-2*dy/height;
        vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queries,0);
        flow_record(&flow,cb,set,&in,params);
        vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,1);
        int save=strcmp(output,"-")!=0;
        if(save){
            VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={width,height,1}};
            vkCmdCopyImageToBuffer(cb,velocity.image,general,readback.buffer,1,&copy);
            copy.bufferOffset=pixels*4;vkCmdCopyImageToBuffer(cb,flow.images[FlowMask].image,general,readback.buffer,1,&copy);
            flow_memory(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
        }
        CHECK(vkEndCommandBuffer(cb));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};
        CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));CHECK(vkQueueWaitIdle(queue));
        uint64_t ticks[2];CHECK(vkGetQueryPoolResults(device,queries,0,2,sizeof ticks,ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT));
        uint64_t elapsed=ticks[1]-ticks[0];if(bits<64)elapsed&=(((uint64_t)1<<bits)-1);
        printf("frame\t%d\t%s\t%.4f\n",frame,input,elapsed*props.limits.timestampPeriod/1.e6);
        if(save){image_file(output,"flow",readback.bytes,4*pixels,1);image_file(output,"mask",readback.bytes+4*pixels,pixels,1);}
        strcpy(lastInput,input);lastJx=jx;lastJy=jy;frame++;
    }
    fclose(frames);
    destroy_buffer(device,upload);destroy_buffer(device,readback);
    vkDestroyDescriptorPool(device,descriptors,NULL);flow_destroy(&flow);
    for(int i=0;i<4;++i){vkDestroyImageView(device,inputs[i].view,NULL);vkDestroyImage(device,inputs[i].image,NULL);vkFreeMemory(device,inputs[i].memory,NULL);}
    vkDestroyQueryPool(device,queries,NULL);vkDestroyCommandPool(device,pool,NULL);
    vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
    return frame?0:3;
}
