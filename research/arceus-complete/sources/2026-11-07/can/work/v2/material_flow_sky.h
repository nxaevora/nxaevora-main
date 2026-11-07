/* GPU material-motion estimator, used by both the renderer and the headless replay.
   Include the Vulkan prototypes or volk before this header. All work is recorded
   onto one graphics/compute queue. The caller owns per-frame descriptor sets. */
#ifndef ARCEUS_MATERIAL_FLOW_GPU_H
#define ARCEUS_MATERIAL_FLOW_GPU_H
#include <stdint.h>
#include <string.h>
#include "../motion/flow_words.h"
#include "seed_sky.h"

enum { FlowMeasure, FlowPropose, FlowScore, FlowChoose, FlowVote, FlowApply, FlowSeed, FlowPassCount };
enum { FlowHistory0, FlowHistory1, FlowStats0, FlowStats1, FlowCandidates, FlowMask, FlowImageCount };
typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
} FlowImage;
typedef struct {
    VkDevice device;
    uint32_t width, height, historyIndex, bucketCount;
    int initialized, warm;
    FlowImage images[FlowImageCount];
    FlowImage nativeMotion;
    VkImageLayout maskLayout;
    VkDescriptorSetLayout setLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipelines[FlowPassCount];
    VkSampler sampler;
} FlowGpu;
typedef struct {
    float clipToPrevious[16];
    float jitter[2], previousJitter[2];
    uint32_t historyValid;
    float refineStep;
} FlowParams;
typedef struct {
    VkImageView color, depth, material, motion;
    VkImageLayout colorLayout, depthLayout, materialLayout;
} FlowInputs;

static void flow_destroy(FlowGpu *g) {
    if (!g->device) return;
    for (int i=0;i<FlowPassCount;++i) vkDestroyPipeline(g->device,g->pipelines[i],NULL);
    vkDestroyPipelineLayout(g->device,g->pipelineLayout,NULL);
    vkDestroyDescriptorSetLayout(g->device,g->setLayout,NULL);
    vkDestroySampler(g->device,g->sampler,NULL);
    for (int i=0;i<=FlowImageCount;++i) {
        FlowImage *image=i==FlowImageCount?&g->nativeMotion:&g->images[i];
        vkDestroyImageView(g->device,image->view,NULL);
        vkDestroyImage(g->device,image->image,NULL);
        vkFreeMemory(g->device,image->memory,NULL);
    }
    memset(g,0,sizeof *g);
}

static VkResult flow_make_image(FlowGpu *g,VkPhysicalDevice physical,FlowImage *out,
                                uint32_t w,uint32_t h,VkFormat format,VkImageUsageFlags usage) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(physical,format,&props);
    VkFormatFeatureFlags required=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (format==VK_FORMAT_R32_SINT) required |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;
    if ((props.optimalTilingFeatures&required)!=required) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkImageCreateInfo create={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=VK_IMAGE_TYPE_2D,.format=format,.extent={w,h,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage};
    VkResult status=vkCreateImage(g->device,&create,NULL,&out->image);
    if (status!=VK_SUCCESS) return status;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g->device,out->image,&req);
    VkPhysicalDeviceMemoryProperties types;
    vkGetPhysicalDeviceMemoryProperties(physical,&types);
    uint32_t type=0;
    while(type<types.memoryTypeCount && (!(req.memoryTypeBits & (1u<<type)) ||
        !(types.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))) ++type;
    if (type==types.memoryTypeCount) return VK_ERROR_FEATURE_NOT_PRESENT;
    VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
    status=vkAllocateMemory(g->device,&alloc,NULL,&out->memory);
    if (status!=VK_SUCCESS) return status;
    status=vkBindImageMemory(g->device,out->image,out->memory,0);
    if (status!=VK_SUCCESS) return status;
    VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=out->image,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    return vkCreateImageView(g->device,&view,NULL,&out->view);
}

static int flow_is_sampler(int binding) { return binding<3 || binding==4; }

static VkResult flow_create(FlowGpu *g,VkPhysicalDevice physical,VkDevice device,
                            uint32_t width,uint32_t height,VkPipelineCache cache) {
    if (!width || !height || width>2560 || height>1440) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical,&props);
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(physical,&features);
    if(!features.shaderStorageImageExtendedFormats)return VK_ERROR_FEATURE_NOT_PRESENT;
    if (props.limits.maxPerStageDescriptorStorageImages<6) return VK_ERROR_FEATURE_NOT_PRESENT;
    memset(g,0,sizeof *g);
    g->device=device; g->width=width; g->height=height;
    g->bucketCount=1+2*((width+192+383)/384)*((height+192+383)/384);
    VkResult result;
    for (int i=0;i<FlowImageCount;++i) {
        uint32_t w=width,h=height;
        VkFormat format=VK_FORMAT_R32G32_UINT;
        VkImageUsageFlags usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (i==FlowStats0 || i==FlowStats1) {
            w=25*g->bucketCount;h=1024;format=VK_FORMAT_R32_SINT;
            usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        } else if (i==FlowCandidates) {
            w=g->bucketCount;h=1024;format=VK_FORMAT_R16G16B16A16_SFLOAT;usage=VK_IMAGE_USAGE_STORAGE_BIT;
        } else if (i==FlowMask) {
            format=VK_FORMAT_R8_UNORM;
            usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        result=flow_make_image(g,physical,&g->images[i],w,h,format,usage);
        if (result!=VK_SUCCESS) goto fail;
    }
    VkDescriptorSetLayoutBinding bindings[10];
    for(int i=0;i<10;++i) {
        bindings[i]=(VkDescriptorSetLayoutBinding){.binding=(uint32_t)i,.descriptorCount=1,
            .descriptorType=flow_is_sampler(i)?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
    }
    VkDescriptorSetLayoutCreateInfo dsl={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                        .bindingCount=10,.pBindings=bindings};
    result=vkCreateDescriptorSetLayout(device,&dsl,NULL,&g->setLayout);
    if (result!=VK_SUCCESS) goto fail;
    VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(FlowParams)};
    VkPipelineLayoutCreateInfo layout={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=&g->setLayout,.pushConstantRangeCount=1,.pPushConstantRanges=&range};
    result=vkCreatePipelineLayout(device,&layout,NULL,&g->pipelineLayout);
    if (result!=VK_SUCCESS) goto fail;
    VkSamplerCreateInfo sampler={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    result=vkCreateSampler(device,&sampler,NULL,&g->sampler);
    if (result!=VK_SUCCESS) goto fail;
    const uint32_t *words[]={measure_spv,candidates_spv,score_spv,choose_spv,validate_spv,apply_spv,sky_seed_words};
    const size_t lengths[]={sizeof measure_spv,sizeof candidates_spv,sizeof score_spv,sizeof choose_spv,sizeof validate_spv,sizeof apply_spv,sizeof sky_seed_words};
    for(int i=0;i<FlowPassCount;++i) {
        VkShaderModuleCreateInfo shader={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=lengths[i],.pCode=words[i]};
        VkShaderModule module;
        result=vkCreateShaderModule(device,&shader,NULL,&module);
        if (result!=VK_SUCCESS) goto fail;
        VkComputePipelineCreateInfo pipe={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
                    .module=module,.pName="main"},.layout=g->pipelineLayout};
        result=vkCreateComputePipelines(device,cache,1,&pipe,NULL,&g->pipelines[i]);
        vkDestroyShaderModule(device,module,NULL);
        if (result!=VK_SUCCESS) goto fail;
    }
    return VK_SUCCESS;
fail:
    flow_destroy(g);
    return result;
}

static void flow_image_layout(VkCommandBuffer cmd,VkImage image,VkImageLayout oldLayout,VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=oldLayout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.oldLayout=oldLayout,.newLayout=newLayout,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&barrier);
}
static void flow_memory(VkCommandBuffer cmd,VkPipelineStageFlags src,VkAccessFlags srcAccess,
                        VkPipelineStageFlags dst,VkAccessFlags dstAccess) {
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=srcAccess,.dstAccessMask=dstAccess};
    vkCmdPipelineBarrier(cmd,src,dst,0,1,&barrier,0,NULL,0,NULL);
}
static void flow_mask_layout(FlowGpu *g,VkCommandBuffer cmd,VkImageLayout layout) {
    if (layout==g->maskLayout) return;
    flow_image_layout(cmd,g->images[FlowMask].image,g->maskLayout,layout);
    g->maskLayout=layout;
}
static void flow_record(FlowGpu *g,VkCommandBuffer cmd,VkDescriptorSet set,const FlowInputs *in,FlowParams params) {
    const VkImageLayout general=VK_IMAGE_LAYOUT_GENERAL;
    if (!g->initialized) {
        for(int i=0;i<FlowImageCount;++i)
            flow_image_layout(cmd,g->images[i].image,VK_IMAGE_LAYOUT_UNDEFINED,general);
        if(g->nativeMotion.image)flow_image_layout(cmd,g->nativeMotion.image,VK_IMAGE_LAYOUT_UNDEFINED,general);
        g->initialized=1;
        g->maskLayout=general;
    }
    flow_mask_layout(g,cmd,general);
    const int previous=g->historyIndex, next=1-previous;
    params.historyValid=params.historyValid && g->warm;
    flow_memory(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    const VkClearColorValue zero={0};
    const VkImageSubresourceRange sub={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdClearColorImage(cmd,g->images[FlowStats0+next].image,general,&zero,1,&sub);
    flow_memory(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    const VkImageView views[]={in->color,in->depth,in->material,in->motion,
        g->images[previous].view,g->images[next].view,g->images[FlowStats0+previous].view,
        g->images[FlowStats0+next].view,g->images[FlowCandidates].view,g->images[FlowMask].view};
    VkDescriptorImageInfo info[10];
    VkWriteDescriptorSet writes[10];
    for(int i=0;i<10;++i) {
        VkImageLayout layout=i==0?in->colorLayout:i==1?in->depthLayout:i==2?in->materialLayout:general;
        info[i]=(VkDescriptorImageInfo){flow_is_sampler(i)?g->sampler:VK_NULL_HANDLE,views[i],layout};
        writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,
            .dstBinding=(uint32_t)i,.descriptorCount=1,.descriptorType=flow_is_sampler(i)?
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,.pImageInfo=&info[i]};
    }
    vkUpdateDescriptorSets(g->device,10,writes,0,NULL);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,g->pipelineLayout,0,1,&set,0,NULL);
    vkCmdPushConstants(cmd,g->pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof params,&params);
    const int passes[]={FlowSeed,FlowMeasure,FlowPropose,FlowScore,FlowChoose,FlowScore,FlowChoose,
                        FlowScore,FlowChoose,FlowScore,FlowChoose,FlowVote,FlowApply};
    for(unsigned pass=g->nativeMotion.image?0:1;pass<sizeof passes/sizeof passes[0];++pass) {
        int i=passes[pass];
        if(i==FlowScore){
            params.refineStep=pass==3?3.0f:pass==5?1.0f:pass==7?.5f:.25f;
            vkCmdPushConstants(cmd,g->pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof params,&params);
        }
        uint32_t groupX=(g->width+31)/32,groupY=(g->height+31)/32;
        if (i==FlowPropose || i==FlowChoose) {groupX=16;groupY=g->bucketCount;}
        if (i==FlowApply || i==FlowSeed) {groupX=(g->width+7)/8;groupY=(g->height+7)/8;}
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,g->pipelines[i]);
        vkCmdDispatch(cmd,groupX,groupY,1);
        flow_memory(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
    }
    g->historyIndex=next;
    g->warm=1;
}
#endif
