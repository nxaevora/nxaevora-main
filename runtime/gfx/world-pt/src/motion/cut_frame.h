/* A single NGX reset left small history-dependent differences on this driver.
   Re-evaluate the same current frame after a memory dependency only on resets.
   The following ordinary frame then starts from the new scene in both passes. */
static bool temporal_ngx_record(VkDevice device,VkCommandBuffer cmd,const VkFsr2Frame *frame) {
    if(!vk_dlaa_record(device,cmd,frame))return false;
    if(!frame->reset)return true;
    flow_memory(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
    return vk_dlaa_record(device,cmd,frame);
}
