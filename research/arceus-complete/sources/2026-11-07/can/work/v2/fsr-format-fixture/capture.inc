#include <stdio.h>
#include <stdlib.h>

static VKAPI_ATTR VkResult VKAPI_CALL fsr_shader_module(VkDevice device, const VkShaderModuleCreateInfo *info,
        const VkAllocationCallbacks *allocator, VkShaderModule *module) {
    if(const char *directory=getenv("ARCEUS_FSR_CAPTURE")){
        static unsigned serial;
        char path[1024];
        snprintf(path,sizeof path,"%s/module-%u.spv",directory,++serial);
        if(FILE *f=fopen(path,"wb")){fwrite(info->pCode,1,info->codeSize,f);fclose(f);}
    }
    return vkCreateShaderModule(device,info,allocator,module);
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fsr_device_proc(VkDevice device,const char *name){
    if(!strcmp(name,"vkCreateShaderModule")) return (PFN_vkVoidFunction)fsr_shader_module;
    return vkGetDeviceProcAddr(device,name);
}
