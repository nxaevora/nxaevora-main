from pathlib import Path
p=Path(__file__).resolve().parent
f=p/'check.c';s=f.read_text().replace('640,.h=360,.ow=960,.oh=540','1920,.h=1080,.ow=2560,.oh=1440').replace('480,270,0','1280,720,0').replace('640x360 -> 960x540','1920x1080 -> 2560x1440')
a=s.index('if(!(dl?vk_dlaa_record')
s=s[:a]+'''for(int i=0;i<3;i++){VkImageMemoryBarrier ib={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT,.oldLayout=VK_IMAGE_LAYOUT_GENERAL,.newLayout=dl?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=imgs[i],.subresourceRange=sub};vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,0,0,0,1,&ib);}
for(int tick=0;tick<3;tick++){frame.reset=tick==0;frame.jx=tick?.25f:0;
'''+s[a:]
s=s.replace('return 4;\nVkBuffer','return 4; mb.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&mb,0,0,0,0);}\nVkBuffer')
f.write_text(s)
f=p/'split.inc';s=f.read_text().replace('tuse(cb,&th[i/2][i%2],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);','tuse(cb,&th[i/2][i%2],vk_dlaa_wanted()?rw:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);');f.write_text(s)
