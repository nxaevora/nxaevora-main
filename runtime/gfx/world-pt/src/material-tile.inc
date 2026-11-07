#include "material-tile-words.h"
enum { materialTileCount=256, materialTileBytes=64*64*8 };
typedef struct {
    VkImage image;
    VkImageView view;
    VkSampler sampler;
    u64 write,hash;
    u32 generation,frame;
} MaterialTile;
static struct {
    VkDescriptorSetLayout descriptors;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    ContactBuffer pixels;
    MaterialTile entries[materialTileCount];
    u32 filled,reused,requests,rejectedWrap,unavailable;
    bool attempted;
} tileCache;

static bool tile_cache_init(void){
    if(tileCache.attempted)return tileCache.pipeline!=VK_NULL_HANDLE;
    tileCache.attempted=true;
    VkDescriptorSetLayoutBinding binding={0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo ds={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    if(vkCreateDescriptorSetLayout(g_vk.dev,&ds,NULL,&tileCache.descriptors)!=VK_SUCCESS)return false;
    VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(u64)};
    VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&tileCache.descriptors,
        .pushConstantRangeCount=1,.pPushConstantRanges=&range};
    if(vkCreatePipelineLayout(g_vk.dev,&pl,NULL,&tileCache.layout)!=VK_SUCCESS)goto fail;
    if(!contact_buffer(&tileCache.pixels,materialTileCount*materialTileBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false))goto fail;
    VkShaderModule mod=vk_module_spv(material_tile_words,sizeof material_tile_words/4);
    if(!mod)goto fail;
    VkComputePipelineCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=tileCache.layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=mod,.pName="main"}};
    VkResult result=vkCreateComputePipelines(g_vk.dev,vk_pipeline_cache(),1,&cp,NULL,&tileCache.pipeline);
    vkDestroyShaderModule(g_vk.dev,mod,NULL);
    if(result==VK_SUCCESS)return true;
fail:
    vkDestroyBuffer(g_vk.dev,tileCache.pixels.handle,NULL);
    vkFreeMemory(g_vk.dev,tileCache.pixels.memory,NULL);
    vkDestroyPipelineLayout(g_vk.dev,tileCache.layout,NULL);
    vkDestroyDescriptorSetLayout(g_vk.dev,tileCache.descriptors,NULL);
    return false;
}

static u64 material_color_tile(VkCommandBuffer cb,const WorldTexture *tex,VkImageLayout layout,u32 *wrap){
    const u32 u=tex->tsc[0]&7,v=(tex->tsc[0]>>3)&7;
    tileCache.requests++;
    if(u==3 || u>4 || v==3 || v>4){tileCache.rejectedWrap++;return 0;}
    if(!tile_cache_init())return 0;
    *wrap=u|(v<<3);
    Surf *s=vk_surf_first(tex->address);
    while(s && (s->img!=tex->image || s->gva!=tex->address))s=s->next;
    if(!s || s->layout==VK_IMAGE_LAYOUT_UNDEFINED || (!s->upload_valid && !s->wseq)){tileCache.unavailable++;return 0;}
    VkImageView view=vk_tex_bind_view(s,VK_IMAGE_VIEW_TYPE_2D,tex->tic,tex->tsc,0);
    VkSampler sampler=vk_tex_bind_sampler(tex->tsc,tex->tic,&tex->shader,0,s,VK_IMAGE_VIEW_TYPE_2D);
    if(!view || !sampler)return 0;
    u32 oldest=UINT32_MAX;
    for(u32 j=0;j<materialTileCount;j++){
        MaterialTile *entry=tileCache.entries+j;
        if(entry->view==view && entry->sampler==sampler && entry->image==s->img && entry->write==s->wseq &&
           entry->generation==s->wgen && entry->hash==s->hash){
            entry->frame=g_vk_present;tileCache.reused++;
            return tileCache.pixels.address+(u64)j*materialTileBytes;
        }
        if((!entry->image || entry->frame!=g_vk_present) && (oldest==UINT32_MAX || entry->frame<tileCache.entries[oldest].frame))oldest=j;
    }
    if(oldest==UINT32_MAX)return 0;
    VkDescriptorSet set=vk_desc_alloc(tileCache.descriptors);
    if(!set)return 0;
    vk_surf_touch(s);vk_surf_use(cb,s,layout,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    VkDescriptorImageInfo image={sampler,view,layout};
    VkWriteDescriptorSet update={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,
        .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&image};
    vkUpdateDescriptorSets(g_vk.dev,1,&update,0,NULL);
    vk_rec_sync();
    if(tileCache.filled==0)contact_barrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    u64 address=tileCache.pixels.address+(u64)oldest*materialTileBytes;
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tileCache.pipeline);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tileCache.layout,0,1,&set,0,NULL);
    vkCmdPushConstants(cb,tileCache.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof address,&address);
    vkCmdDispatch(cb,8,8,1);
    tileCache.entries[oldest]=(MaterialTile){.image=s->img,.view=view,.sampler=sampler,.write=s->wseq,.hash=s->hash,
        .generation=s->wgen,.frame=g_vk_present};
    tileCache.filled++;
    return address;
}
