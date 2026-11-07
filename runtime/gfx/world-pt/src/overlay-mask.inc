static struct {
    THist image;
    VkDescriptorSetLayout descriptors;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    bool attempted;
} overlayMask;

static bool overlay_mask_init(u32 w,u32 h){
    if(overlayMask.attempted)return overlayMask.pipeline!=VK_NULL_HANDLE;
    overlayMask.attempted=true;
    if(!denoise_image(&overlayMask.image,VK_FORMAT_R8_UNORM,w,h,0))goto fail;
    VkDescriptorSetLayoutBinding bindings[3];
    for(u32 i=0;i<3;i++)bindings[i]=(VkDescriptorSetLayoutBinding){i,i==2?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo ds={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=bindings};
    if(vkCreateDescriptorSetLayout(g_vk.dev,&ds,NULL,&overlayMask.descriptors)!=VK_SUCCESS)goto fail;
    VkPipelineLayoutCreateInfo layout={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&overlayMask.descriptors};
    if(vkCreatePipelineLayout(g_vk.dev,&layout,NULL,&overlayMask.layout)!=VK_SUCCESS)goto fail;
    VkShaderModule shader=vk_module_spv(overlay_mask_words,sizeof overlay_mask_words/4);
    if(!shader)goto fail;
    VkComputePipelineCreateInfo create={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=overlayMask.layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"}};
    VkResult result=vkCreateComputePipelines(g_vk.dev,vk_pipeline_cache(),1,&create,NULL,&overlayMask.pipeline);
    vkDestroyShaderModule(g_vk.dev,shader,NULL);
    if(result==VK_SUCCESS)return true;
fail:
    vkDestroyPipelineLayout(g_vk.dev,overlayMask.layout,NULL);
    vkDestroyDescriptorSetLayout(g_vk.dev,overlayMask.descriptors,NULL);
    vkDestroyImageView(g_vk.dev,overlayMask.image.view,NULL);
    if(overlayMask.image.im)vk_image_destroy(overlayMask.image.im,overlayMask.image.mem);
    LOG("world-pt: reactive overlay mask unavailable\n");
    return false;
}

static void overlay_mask_record(VkCommandBuffer cb,SceneResolve *input){
    if(!overlayMask.pipeline)return;
    VkDescriptorSet set=vk_desc_alloc(overlayMask.descriptors);
    if(!set)return;
    VkImageLayout ro=input->dl?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    THist *mask=&overlayMask.image;
    rr_transition(cb,mask,VK_IMAGE_LAYOUT_GENERAL);
    VkDescriptorImageInfo info[]={{tsamp,th[0][0].view,ro},{tsamp,input->f.color_view,ro},{VK_NULL_HANDLE,mask->view,VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[3];
    for(u32 i=0;i<3;i++)writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=i==2?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info[i]};
    vkUpdateDescriptorSets(g_vk.dev,3,writes,0,NULL);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,overlayMask.pipeline);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,overlayMask.layout,0,1,&set,0,NULL);
    vkCmdDispatch(cb,(input->f.w+7)/8,(input->f.h+7)/8,1);
    rr_transition(cb,mask,ro);
    contact_barrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
    input->f.reactive=mask->im;input->f.reactive_view=mask->view;
    if(getenv("ARCEUS_RT_VERIFY") && g_vk_present%120==0)LOG("world-pt: composite reactive mask at present %u\n",g_vk_present);
}
