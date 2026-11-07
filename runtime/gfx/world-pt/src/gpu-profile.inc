enum { profileWindows=32,profileMarks=9 };
static struct {
    VkQueryPool pool;
    u64 mask;
    u32 frames[profileWindows],written[profileWindows];
    bool checked;
} gpuProfile;
static bool profile_init(void){
    if(gpuProfile.checked)return gpuProfile.pool!=VK_NULL_HANDLE;
    gpuProfile.checked=true;
    if(!getenv("ARCEUS_QA_GPU_TIME"))return false;
    u32 count=0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.phys,&count,NULL);
    if(!count || g_vk.gfx_family>=count)return false;
    VkQueueFamilyProperties *families=calloc(count,sizeof(*families));if(!families)return false;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.phys,&count,families);
    u32 bits=families[g_vk.gfx_family].timestampValidBits;
    free(families);
    if(!bits || bits>64)return false;
    gpuProfile.mask=bits==64?UINT64_MAX:((1ull<<bits)-1);
    VkQueryPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,.queryType=VK_QUERY_TYPE_TIMESTAMP,
        .queryCount=profileWindows*profileMarks};
    return vkCreateQueryPool(g_vk.dev,&ci,NULL,&gpuProfile.pool)==VK_SUCCESS;
}
static void profile_mark(VkCommandBuffer cb,u32 mark){
    if((g_vk_present&7) || !profile_init())return;
    u32 slot=(g_vk_present/8)%profileWindows,offset=slot*profileMarks;
    u32 prior=gpuProfile.written[slot];
    if(mark==1){
        if(prior && gpuProfile.frames[slot]==g_vk_present)return;
        if(prior){
            u64 values[profileMarks][2]={0};
            VkResult result=vkGetQueryPoolResults(g_vk.dev,gpuProfile.pool,offset,profileMarks,
                sizeof values,values,sizeof values[0],VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            if(result==VK_SUCCESS || result==VK_NOT_READY){
                for(u32 i=1;i<profileMarks;i++){
                    if(!((prior>>(i-1))&1) || !((prior>>i)&1) || !values[i][1] || !values[i-1][1])continue;
                    u64 ticks=(values[i][0]-values[i-1][0])&gpuProfile.mask;
                    LOG("world-gpu: frame=%u mark=%u us=%.2f\n",gpuProfile.frames[slot],i+1,ticks*(double)g_vk.props.limits.timestampPeriod/1000.);
                }
            }
        }
        gpuProfile.written[slot]=0;gpuProfile.frames[slot]=g_vk_present;
        vkCmdResetQueryPool(cb,gpuProfile.pool,offset,profileMarks);
    }
    if((mark!=1 && !(gpuProfile.written[slot]&1u)) || gpuProfile.frames[slot]!=g_vk_present || (gpuProfile.written[slot]&(1u<<(mark-1))))return;
    vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,gpuProfile.pool,offset+mark-1);
    gpuProfile.written[slot]|=1u<<(mark-1);
}
