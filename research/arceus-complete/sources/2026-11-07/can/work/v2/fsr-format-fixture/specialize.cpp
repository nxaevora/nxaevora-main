#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static const char *outpath;
static VKAPI_ATTR VkResult VKAPI_CALL save(VkDevice,const VkShaderModuleCreateInfo *ci,const VkAllocationCallbacks*,VkShaderModule*) {
    FILE *f=fopen(outpath,"wb");
    if(!f)return VK_ERROR_UNKNOWN;
    bool ok=fwrite(ci->pCode,1,ci->codeSize,f)==ci->codeSize;
    fclose(f);
    return ok?VK_SUCCESS:VK_ERROR_UNKNOWN;
}
#define vkCreateShaderModule save
#include "../fsr-formats.inc"
int main(int argc,char **argv){
    if(argc!=3)return 2;
    outpath=argv[2];
    FILE *f=fopen(argv[1],"rb");
    if(!f)return 3;
    fseek(f,0,SEEK_END);
    long sz=ftell(f);
    rewind(f);
    uint32_t *p=(uint32_t*)malloc(sz);
    if(!p || fread(p,1,sz,f)!=(size_t)sz)return 4;
    fclose(f);
    VkShaderModuleCreateInfo ci={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.pCode=p;ci.codeSize=sz;
    fsr_output_format=ImageFormat::Rgba16f;
    return fsr_shader_module(nullptr,&ci,nullptr,nullptr)!=VK_SUCCESS;
}
