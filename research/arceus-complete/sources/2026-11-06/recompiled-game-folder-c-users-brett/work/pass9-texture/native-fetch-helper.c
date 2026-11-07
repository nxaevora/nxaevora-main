/* Private native point-sampling control, using the same 64x64 compressed image as bc_native_case. */
static void native_fetch_compare(VkImage src, VkFormat format, int kind, const float* blit_pixels) {
    enum { SIZE = 64*64*16 };
    float* expected = malloc(SIZE); memcpy(expected, blit_pixels, SIZE);
    FILE* input = fopen("work/pass9-texture/native-fetch.spv", "rb");
    if (!input) fatal("cannot read native-fetch.spv\n");
    fseek(input, 0, SEEK_END); long length = ftell(input); rewind(input);
    u32* code = malloc(length); if (fread(code, 1, length, input) != (size_t)length) fatal("short SPV read\n"); fclose(input);
    VkShaderModuleCreateInfo mi = {.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)length,.pCode=code};
    VkShaderModule module; VK_OK(vkCreateShaderModule(g_vk.dev,&mi,NULL,&module)); free(code);
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
    VkDescriptorSetLayoutCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};
    VkDescriptorSetLayout ds; VK_OK(vkCreateDescriptorSetLayout(g_vk.dev,&di,NULL,&ds));
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&ds};
    VkPipelineLayout layout; VK_OK(vkCreatePipelineLayout(g_vk.dev,&li,NULL,&layout));
    VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=layout};
    VkPipeline pipeline; VK_OK(vkCreateComputePipelines(g_vk.dev,VK_NULL_HANDLE,1,&ci,NULL,&pipeline));
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=src,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    VkImageView view; VK_OK(vkCreateImageView(g_vk.dev,&vi,NULL,&view));
    VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,
        .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.maxLod=0};
    VkSampler sampler; VK_OK(vkCreateSampler(g_vk.dev,&si,NULL,&sampler));
    u64 cap; VkBuffer scratch=vk_bl_scratch(SIZE,&cap), rb;
    VkDescriptorSet set=vk_desc_alloc(ds); if (!set) fatal("no native point-fetch descriptor\n");
    VkDescriptorImageInfo im={sampler,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo buf={scratch,0,SIZE};
    VkWriteDescriptorSet writes[2]={
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&im},
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&buf}};
    vkUpdateDescriptorSets(g_vk.dev,2,writes,0,NULL);
    barrier(src,VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_READ_BIT,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk_bl_barrier(vk_cmd(),1);
    rec_CmdBindPipeline(vk_cmd(),VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    rec_CmdBindDescriptorSets(vk_cmd(),VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
    rec_CmdDispatch(vk_cmd(),8,8,1);
    vk_bl_barrier(vk_cmd(),0);
    float* got=(float*)vk_readback_buffer(SIZE,&rb);
    VkBufferCopy copy={0,0,SIZE}; rec_CmdCopyBuffer(vk_cmd(),scratch,rb,1,&copy);
    vk_finish(); vk_readback_ready();
    unsigned differ=0; float worst=0;
    for (unsigned i=0;i<SIZE/4;i++) { float d=fabsf(got[i]-expected[i]); if (d>worst)worst=d; if (d>0.00000012f) { if (differ++<4) fprintf(stderr,"native point-fetch kind%d channel%u: fetch%.9g blit%.9g\n",kind,i,got[i],expected[i]); } }
    char name[256]; snprintf(name,sizeof name,"work/pass9-texture/native-%d-fetch.bin",kind);
    FILE* dump=fopen(name,"wb"); fwrite(got,1,SIZE,dump); fclose(dump);
    if (differ) native_failures++;
    printf("%s native BCn kind %d point-fetch versus native blit (%u differing channels; worst %.9g)\n",differ?"FAIL":"PASS",kind,differ,worst);
    vkDestroySampler(g_vk.dev,sampler,NULL); vkDestroyImageView(g_vk.dev,view,NULL);
    vkDestroyPipeline(g_vk.dev,pipeline,NULL); vkDestroyPipelineLayout(g_vk.dev,layout,NULL);
    vkDestroyDescriptorSetLayout(g_vk.dev,ds,NULL); vkDestroyShaderModule(g_vk.dev,module,NULL); free(expected);
}
