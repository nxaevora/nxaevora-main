typedef struct {FlowParams motion; VkDeviceAddress primaryLight; u32 layer;} FilterParams;
_Static_assert(offsetof(FilterParams,primaryLight)==88,"filter push constant alignment");
static struct {
    THist colors[2], metadata[2];
    VkDescriptorSetLayout bindings;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    u32 last, index;
    bool attempted, warm;
    bool hadMaterial;
    float jitter[2];
} denoiser;

static bool denoise_image(THist *image, VkFormat format,u32 w,u32 h,VkImageUsageFlags extra){
    VkImageCreateInfo create={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .extent={w,h,1},.format=format,.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|extra};
    if(!vk_image_create(&create,&image->im,&image->mem))return false;
    VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image->im,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    return vkCreateImageView(g_vk.dev,&view,NULL,&image->view)==VK_SUCCESS;
}
static bool denoise_init(u32 w,u32 h){
    for(int i=0;i<2;i++){
        if(!denoise_image(&denoiser.colors[i],VK_FORMAT_R16G16B16A16_SFLOAT,w,h,0) ||
           !denoise_image(&denoiser.metadata[i],VK_FORMAT_R32G32_UINT,w,h,0))return false;
    }
    VkDescriptorSetLayoutBinding b[9];
    for(u32 i=0;i<9;i++)b[i]=(VkDescriptorSetLayoutBinding){i,(i<6 || i==8)?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo desc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=9,.pBindings=b};
    if(vkCreateDescriptorSetLayout(g_vk.dev,&desc,NULL,&denoiser.bindings))return false;
    VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(FilterParams)};
    VkPipelineLayoutCreateInfo layout={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,
        .pSetLayouts=&denoiser.bindings,.pushConstantRangeCount=1,.pPushConstantRanges=&range};
    if(vkCreatePipelineLayout(g_vk.dev,&layout,NULL,&denoiser.layout))return false;
    VkShaderModule module=vk_module_spv(denoise_words,sizeof denoise_words/4);
    if(!module)return false;
    VkComputePipelineCreateInfo pipe={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=denoiser.layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"}};
    VkResult result=vkCreateComputePipelines(g_vk.dev,vk_pipeline_cache(),1,&pipe,NULL,&denoiser.pipeline);
    vkDestroyShaderModule(g_vk.dev,module,NULL);
    return result==VK_SUCCESS;
}
static void denoise_record(VkCommandBuffer cb,VkFsr2Frame *f,VkImageView albedo,VkImageView material,FlowParams params,VkImageLayout inputLayout){
    if(!denoiser.attempted){
        denoiser.attempted=true;
        const char *enable=getenv("ARCEUS_PT_FILTER");
        if(!enable || strcmp(enable,"0")){
            bool ready=denoise_init(f->w,f->h);
            if(!ready){
                vkDestroyPipelineLayout(g_vk.dev,denoiser.layout,NULL);
                vkDestroyDescriptorSetLayout(g_vk.dev,denoiser.bindings,NULL);
                for(unsigned i=0;i<2;i++){
                    THist *images[]={&denoiser.colors[i],&denoiser.metadata[i]};
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
    if(!denoiser.pipeline)return;
    VkDescriptorSet set=vk_desc_alloc(denoiser.bindings);
    if(!set)return;
    const VkImageLayout rw=VK_IMAGE_LAYOUT_GENERAL;
    u32 next=denoiser.index^1,prev=denoiser.index;
    for(u32 i=0;i<2;i++){
        THist *c=&denoiser.colors[i],*m=&denoiser.metadata[i];
        flow_image_layout(cb,c->im,c->layout,rw);c->layout=rw;
        flow_image_layout(cb,m->im,m->layout,rw);m->layout=rw;
    }
    flow_memory(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
    VkImageView views[]={f->color_view,f->depth_view,f->mv_view,albedo,denoiser.colors[prev].view,
        denoiser.metadata[prev].view,denoiser.colors[next].view,denoiser.metadata[next].view,material?material:albedo};
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
    params.historyValid=(!hasMaterial || params.historyValid) && hasMaterial==denoiser.hadMaterial &&
        denoiser.warm && !f->reset && denoiser.last+1==g_vk_present;
    memcpy(params.previousJitter,denoiser.jitter,sizeof params.previousJitter);
    params.refineStep=hasMaterial?1.f:0.f;
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,denoiser.pipeline);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,denoiser.layout,0,1,&set,0,NULL);
    FilterParams filter={params,contact.primaryLight.address,g_vk_present&1u};
    vkCmdPushConstants(cb,denoiser.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof filter,&filter);
    vkCmdDispatch(cb,(f->w+7)/8,(f->h+7)/8,1);
    flow_image_layout(cb,denoiser.colors[next].im,rw,inputLayout);
    denoiser.colors[next].layout=inputLayout;
    f->color=denoiser.colors[next].im;f->color_view=denoiser.colors[next].view;
    denoiser.index=next;denoiser.last=g_vk_present;denoiser.warm=true;
    denoiser.hadMaterial=hasMaterial;
    memcpy(denoiser.jitter,params.jitter,sizeof params.jitter);
}
