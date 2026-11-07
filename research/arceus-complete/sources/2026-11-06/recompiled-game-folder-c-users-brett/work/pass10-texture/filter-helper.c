static u16* cpu_mips[11];
static float reference_bilinear(int level, float u, float v, int channel, float* quant_bound) {
    int w=1024>>level; if(w<1)w=1;
    float x=u*w-0.5f,y=v*w-0.5f;int ix=(int)floorf(x),iy=(int)floorf(y);
    float fx=x-floorf(x),fy=y-floorf(y),q[2][2];
    for(int yy=0;yy<2;yy++)for(int xx=0;xx<2;xx++) {
        int px=(ix+xx)%w,py=(iy+yy)%w;if(px<0)px+=w;if(py<0)py+=w;
        q[yy][xx]=cpu_mips[level][(py*w+px)*2+channel]/65535.0f;
    }
    float a=q[0][0]*(1-fx)+q[0][1]*fx,b=q[1][0]*(1-fx)+q[1][1]*fx;
    float dx=fmaxf(fabsf(q[0][1]-q[0][0]),fabsf(q[1][1]-q[1][0]));
    float dy=fmaxf(fabsf(q[1][0]-q[0][0]),fabsf(q[1][1]-q[0][1]));
    *quant_bound=(dx+dy)*ldexpf(1.0f,-(int)g_vk.props.limits.subTexelPrecisionBits);
    return a*(1-fy)+b*fy;
}
static float reference_trilinear(float u,float v,float lod,int channel,float* bound) {
    float l=fminf(fmaxf(lod,0),10),frac=l-floorf(l),ba,bb;int a=(int)floorf(l),b=a<10?a+1:a;
    float va=reference_bilinear(a,u,v,channel,&ba),vb=reference_bilinear(b,u,v,channel,&bb);
    *bound=fmaxf(ba,bb)+fabsf(vb-va)*ldexpf(1.0f,-(int)g_vk.props.limits.mipmapPrecisionBits)+0.000002f;
    return va*(1-frac)+vb*frac;
}
static void check_filter(Surf* t) {
    static const float lods[]={-2,-1,-.5f,0,.25f,.5f,.75f,1,1.25f,1.5f,1.75f,2,2.25f,2.5f,3,3.25f,3.5f,4,4.25f,4.5f,5,5.25f,5.5f,6,6.5f,7,7.5f,8,8.5f,9,9.5f,10,10.5f,12,15};
    enum {UVS=128, NLOD=sizeof(lods)/sizeof(lods[0]), COUNT=UVS*NLOD, SIZE=COUNT*16};
    float* queries=malloc(SIZE);
    for(int k=0;k<NLOD;k++)for(int j=0;j<UVS;j++) {
        float* q=queries+(k*UVS+j)*4;
        q[0]=(((j*73)%1536)-256+0.125f*(j&7)+0.5f)/1024;
        q[1]=(((j*157)%1792)-384+0.125f*((j>>3)&7)+0.5f)/1024;
        q[2]=lods[k];q[3]=0;
    }
    FILE* input=fopen("work/pass10-texture/filter.spv","rb");if(!input)fatal("filter shader missing\n");
    fseek(input,0,SEEK_END);long length=ftell(input);rewind(input);u32* code=malloc(length);
    if(fread(code,1,length,input)!=(size_t)length)fatal("shader read failed\n");fclose(input);
    VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)length,.pCode=code};
    VkShaderModule module;VK_OK(vkCreateShaderModule(g_vk.dev,&mi,NULL,&module));free(code);
    VkDescriptorSetLayoutBinding bindings[3]={
        {0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
        {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
    VkDescriptorSetLayoutCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=bindings};
    VkDescriptorSetLayout ds;VK_OK(vkCreateDescriptorSetLayout(g_vk.dev,&di,NULL,&ds));
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&ds};
    VkPipelineLayout layout;VK_OK(vkCreatePipelineLayout(g_vk.dev,&li,NULL,&layout));
    VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=layout};
    VkPipeline pipeline;VK_OK(vkCreateComputePipelines(g_vk.dev,VK_NULL_HANDLE,1,&ci,NULL,&pipeline));
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=t->img,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=t->f.vk,
        .components={VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ONE},
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,t->levels,0,1}};
    VkImageView view;VK_OK(vkCreateImageView(g_vk.dev,&vi,NULL,&view));
    const u32 tsc[8]={0x00026100,0x000003e2,0xfff00000,0x0ffff000,0x3f800000,0x3f800000,0x3f800000,0x3f800000};
    VkSampler sampler=vk_sampler(tsc,0,0);if(!sampler)fatal("captured TSC sampler failed\n");
    u64 cap,at;VkBuffer scratch=vk_bl_scratch(SIZE,&cap),staged,rb;
    memcpy(vk_stage(SIZE,vk_stage_align(),&staged,&at),queries,SIZE);
    VkDescriptorSet set=vk_desc_alloc(ds);if(!set)fatal("descriptor allocation failed\n");
    VkDescriptorImageInfo im={sampler,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo out={scratch,0,SIZE},in={staged,at,SIZE};
    VkWriteDescriptorSet writes[3]={
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&im},
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&out},
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&in}};
    vkUpdateDescriptorSets(g_vk.dev,3,writes,0,NULL);
    vk_surf_use(vk_cmd(),t,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    vk_bl_barrier(vk_cmd(),1);
    rec_CmdBindPipeline(vk_cmd(),VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    rec_CmdBindDescriptorSets(vk_cmd(),VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
    rec_CmdDispatch(vk_cmd(),COUNT/64,1,1);vk_bl_barrier(vk_cmd(),0);
    float* actual=(float*)vk_readback_buffer(SIZE,&rb);VkBufferCopy cp={0,0,SIZE};rec_CmdCopyBuffer(vk_cmd(),scratch,rb,1,&cp);
    vk_finish();vk_readback_ready();
    unsigned failed=0;float worst[2]={0},max_ratio[2]={0};
    FILE* results=fopen("work/pass10-texture/filter-results.csv","w");fprintf(results,"query,mode,u,v,lod,channel,gpu,cpu,error,precision_bound\n");
    for(int i=0;i<COUNT;i++)for(int mode=0;mode<2;mode++)for(int c=0;c<2;c++) {
        float* q=queries+i*4;float bound,e=reference_trilinear(q[0],q[1],q[2],c,&bound),g=actual[i*4+mode*2+c],d=fabsf(g-e);
        if(d>worst[mode])worst[mode]=d;if(d/bound>max_ratio[mode])max_ratio[mode]=d/bound;
        fprintf(results,"%d,%d,%.9g,%.9g,%.9g,%d,%.9g,%.9g,%.9g,%.9g\n",i,mode,q[0],q[1],q[2],c,g,e,d,bound);
        if(!(d<=bound)) {if(failed++<8)fprintf(stderr,"filter query%d mode%d c%d lod%.3g gpu%.9g cpu%.9g d%.9g bound%.9g\n",i,mode,c,q[2],g,e,d,bound);}
    }
    fclose(results);
    printf("Sampling precision bits subtexel%u mipmap%u; captured TSC exact, production vk_sampler, raised anisotropy disabled.\n",g_vk.props.limits.subTexelPrecisionBits,g_vk.props.limits.mipmapPrecisionBits);
    printf("%s filtering: %u coordinates x2channels x2modes; textureLod worst%.9g bound-ratio%.5g; isotropic textureGrad worst%.9g bound-ratio%.5g; failures%u\n",failed?"FAIL":"PASS",COUNT,worst[0],max_ratio[0],worst[1],max_ratio[1],failed);
    if(failed)fatal("BC5 RG16 sampling exceeded local precision bound\n");
    free(queries);vkDestroyImageView(g_vk.dev,view,NULL);vkDestroyPipeline(g_vk.dev,pipeline,NULL);
    vkDestroyPipelineLayout(g_vk.dev,layout,NULL);vkDestroyDescriptorSetLayout(g_vk.dev,ds,NULL);vkDestroyShaderModule(g_vk.dev,module,NULL);
}
