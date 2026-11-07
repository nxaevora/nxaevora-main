typedef struct {FlowParams motion; VkDeviceAddress primaryLight; u32 layer;VkDeviceAddress reference;} FilterParams;
_Static_assert(offsetof(FilterParams,primaryLight)==88,"filter push constant alignment");
typedef struct {
    THist colors[2], metadata[2];
    VkDescriptorSetLayout bindings;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    u32 last, index;
    bool attempted, warm;
    bool hadMaterial;
    float jitter[2];
} DenoiserState;
static DenoiserState denoiser;

static bool denoise_image(THist *image, VkFormat format,u32 w,u32 h,VkImageUsageFlags extra){
    VkImageCreateInfo create={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .extent={w,h,1},.format=format,.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|extra};
    if(!vk_image_create(&create,&image->im,&image->mem))return false;
    VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image->im,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    return vkCreateImageView(g_vk.dev,&view,NULL,&image->view)==VK_SUCCESS;
}
static bool denoise_init(DenoiserState *state,u32 w,u32 h,bool baseline){
    for(int i=0;i<2;i++){
        if(!denoise_image(&state->colors[i],VK_FORMAT_R16G16B16A16_SFLOAT,w,h,0) ||
           !denoise_image(&state->metadata[i],VK_FORMAT_R32G32B32A32_UINT,w,h,0))return false;
    }
    VkDescriptorSetLayoutBinding b[9];
    for(u32 i=0;i<9;i++)b[i]=(VkDescriptorSetLayoutBinding){i,(i<6 || i==8)?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo desc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=9,.pBindings=b};
    if(vkCreateDescriptorSetLayout(g_vk.dev,&desc,NULL,&state->bindings))return false;
    VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(FilterParams)};
    VkPipelineLayoutCreateInfo layout={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,
        .pSetLayouts=&state->bindings,.pushConstantRangeCount=1,.pPushConstantRanges=&range};
    if(vkCreatePipelineLayout(g_vk.dev,&layout,NULL,&state->layout))return false;
    VkShaderModule module;
#ifdef COMPARE_FILTER
    if(baseline)module=vk_module_spv(baseline_words,sizeof baseline_words/4);
    else
#endif
    module=vk_module_spv(denoise_words,sizeof denoise_words/4);
    if(!module)return false;
    VkComputePipelineCreateInfo pipe={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=state->layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"}};
    VkResult result=vkCreateComputePipelines(g_vk.dev,vk_pipeline_cache(),1,&pipe,NULL,&state->pipeline);
    vkDestroyShaderModule(g_vk.dev,module,NULL);
    return result==VK_SUCCESS;
}
static bool denoise_one(DenoiserState *state,bool baseline,VkCommandBuffer cb,VkFsr2Frame *f,VkImageView albedo,VkImageView material,FlowParams params,VkImageLayout inputLayout){
    if(!state->attempted){
        state->attempted=true;
        const char *enable=getenv("ARCEUS_PT_FILTER");
        if(!enable || strcmp(enable,"0")){
            bool ready=denoise_init(state,f->w,f->h,baseline);
            if(!ready){
                vkDestroyPipelineLayout(g_vk.dev,state->layout,NULL);
                vkDestroyDescriptorSetLayout(g_vk.dev,state->bindings,NULL);
                for(unsigned i=0;i<2;i++){
                    THist *images[]={&state->colors[i],&state->metadata[i]};
                    for(unsigned j=0;j<2;j++){
                        THist *image=images[j];
                        vkDestroyImageView(g_vk.dev,image->view,NULL);
                        if(image->im)vk_image_destroy(image->im,image->mem);
                    }
                }
            }
            LOG("arceus-rt: temporal radiance filter %s\n",ready?"ready":"unavailable");
        }
    }
    if(!state->pipeline)return false;
    VkDescriptorSet set=vk_desc_alloc(state->bindings);
    if(!set)return false;
    const VkImageLayout rw=VK_IMAGE_LAYOUT_GENERAL;
    u32 next=state->index^1,prev=state->index;
    for(u32 i=0;i<2;i++){
        THist *c=&state->colors[i],*m=&state->metadata[i];
        flow_image_layout(cb,c->im,c->layout,rw);c->layout=rw;
        flow_image_layout(cb,m->im,m->layout,rw);m->layout=rw;
    }
    flow_memory(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
    VkImageView views[]={f->color_view,f->depth_view,f->mv_view,albedo,state->colors[prev].view,
        state->metadata[prev].view,state->colors[next].view,state->metadata[next].view,material?material:albedo};
    VkDescriptorImageInfo info[9];VkWriteDescriptorSet writes[9];
    for(u32 i=0;i<9;i++){
        bool sampled=i<6 || i==8;
        info[i]=(VkDescriptorImageInfo){sampled?tsamp:VK_NULL_HANDLE,views[i],(i==3 || i==8)?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:rw};
        if(i<2)info[i].imageLayout=inputLayout;
        writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,
            .descriptorCount=1,.descriptorType=sampled?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,.pImageInfo=info+i};
    }
    vkUpdateDescriptorSets(g_vk.dev,9,writes,0,NULL);
    bool hasMaterial=material!=VK_NULL_HANDLE;
    params.historyValid=(!hasMaterial || params.historyValid) && hasMaterial==state->hadMaterial &&
        state->warm && !f->reset && state->last+1==g_vk_present;
    if(!baseline && getenv("ARCEUS_QA_FILTER_HISTORY") && (g_vk_present%120==0 || g_vk_present==1960)){
        LOG("world-pt: filter history frame=%u valid=%u warm=%d reset=%d previous=%u material=%d priorMaterial=%d\n",
            g_vk_present,params.historyValid,state->warm,f->reset,state->last,hasMaterial,state->hadMaterial);
    }
    memcpy(params.previousJitter,state->jitter,sizeof params.previousJitter);
    params.refineStep=hasMaterial?1.f:0.f;
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,state->pipeline);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,state->layout,0,1,&set,0,NULL);
    FilterParams filter={params,contact.primaryLight.address,g_vk_present&1u,
        reference.frame==g_vk_present && !reference.saved?reference.buffer.address:0};
    vkCmdPushConstants(cb,state->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof filter,&filter);
    vkCmdDispatch(cb,(f->w+7)/8,(f->h+7)/8,1);
    if(filter.reference)contact_barrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
    flow_image_layout(cb,state->colors[next].im,rw,inputLayout);
    state->colors[next].layout=inputLayout;
    f->color=state->colors[next].im;f->color_view=state->colors[next].view;
    state->index=next;state->last=g_vk_present;state->warm=true;
    state->hadMaterial=hasMaterial;
    memcpy(state->jitter,params.jitter,sizeof params.jitter);
    return true;
}

static bool denoise_record(VkCommandBuffer cb,VkFsr2Frame *f,VkImageView albedo,VkImageView material,FlowParams params,VkImageLayout inputLayout){
#ifdef COMPARE_FILTER
    static DenoiserState baseline;
    VkFsr2Frame parallel=*f;
    denoise_one(&baseline,true,cb,&parallel,albedo,material,params,inputLayout);
#endif
    return denoise_one(&denoiser,false,cb,f,albedo,material,params,inputLayout);
}
