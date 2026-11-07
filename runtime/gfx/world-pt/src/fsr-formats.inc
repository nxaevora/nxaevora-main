#include <glslang/SPIRV/spirv.hpp11>
#include <stdlib.h>

// FSR 3.1.6's Vulkan provider declares its float UAVs as 32-bit components.
// Specialize the storage-image types to the actual views bound by this provider.
// An OpTypeImage can be shared by two variables backed by different VkFormats.
using ImageFormat = spv::ImageFormat;
using Op = spv::Op;

static ImageFormat fsr_output_format;
static ImageFormat fsr_view_format(const char *name) {
    struct Resource { const char *name; ImageFormat format; };
    static const Resource resources[] = {
        {"rw_accumulation", ImageFormat::R8}, {"rw_new_locks", ImageFormat::R8},
        {"rw_shading_change", ImageFormat::R8}, {"rw_current_luma", ImageFormat::R16f},
        {"rw_farthest_depth", ImageFormat::R16f}, {"rw_farthest_depth_mip1", ImageFormat::R16f},
        {"rw_luma_instability", ImageFormat::R16f}, {"rw_dilated_motion_vectors", ImageFormat::Rg16f},
        {"rw_dilated_reactive_masks", ImageFormat::Rgba8}, {"rw_luma_history", ImageFormat::Rgba16f},
        {"rw_internal_upscaled_color", ImageFormat::Rgba16f},
    };
    if (!strcmp(name, "rw_upscaled_output")) return fsr_output_format;
    if (!strncmp(name, "rw_spd_mip", 10) && name[10] >= '0' && name[10] <= '5' && !name[11])
        return ImageFormat::Rg16f;
    for (const auto &r : resources) if (!strcmp(name, r.name)) return r.format;
    return ImageFormat::Unknown;
}

struct FsrId {
    const uint32_t *definition;
    ImageFormat format;
    uint32_t load_type;
};

struct FsrStorageType {
    uint32_t shape[7];  // sampled scalar type through format, inclusive
    uint32_t storage;
    uint32_t image, pointer;
};

// This hook receives modules from the pinned provider only. It leaves other
// SPIR-V types (in particular atomics on integer storage images) untouched.
static VKAPI_ATTR VkResult VKAPI_CALL fsr_shader_module(VkDevice device, const VkShaderModuleCreateInfo *info,
        const VkAllocationCallbacks *allocator, VkShaderModule *module) {
    const uint32_t *code = info->pCode;
    const size_t words = info->codeSize / sizeof(*code);
    if (words < 5 || code[0] != spv::MagicNumber || !code[3] || code[3] > (1u<<22))
        return vkCreateShaderModule(device,info,allocator,module);
    FsrId *ids = (FsrId *)calloc(code[3], sizeof(FsrId));
    if (!ids) return VK_ERROR_OUT_OF_HOST_MEMORY;
    bool extended = false;
    unsigned named = 0;
    for (size_t i=5; i<words;) {
        const uint32_t *p=code+i;
        const unsigned n=p[0]>>16;
        if (!n || n>words-i) { free(ids); return VK_ERROR_INITIALIZATION_FAILED; }
        const Op op=Op(p[0]&0xffff);
        if (op==Op::OpName && n>=3 && p[1]<code[3] && memchr(p+2,0,(n-2)*4)) {
            ids[p[1]].format=fsr_view_format((const char*)(p+2));
            named += ids[p[1]].format!=ImageFormat::Unknown;
        }
        if ((op==Op::OpTypeImage || op==Op::OpTypePointer) && n>=4 && p[1]<code[3])
            ids[p[1]].definition=p;
        if (op==Op::OpCapability && n==2 && p[1]==unsigned(spv::Capability::StorageImageExtendedFormats)) extended=true;
        i+=n;
    }
    if (!named) { free(ids); return vkCreateShaderModule(device,info,allocator,module); }
    uint32_t *result=(uint32_t*)malloc((words+2+named*13)*sizeof(*result));
    FsrStorageType *types=(FsrStorageType*)calloc(named,sizeof(*types));
    if (!result || !types) { free(ids); free(result); free(types); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    memcpy(result,code,20);
    size_t used=5;
    unsigned type_count=0;
    if (!extended) {
        result[used++]=(2u<<16)|unsigned(Op::OpCapability);
        result[used++]=unsigned(spv::Capability::StorageImageExtendedFormats);
    }
    for (size_t i=5; i<words;) {
        const uint32_t *p=code+i;
        const unsigned n=p[0]>>16;
        const Op op=Op(p[0]&0xffff);
        unsigned replacement=0;
        if (op==Op::OpVariable && n==4 && p[2]<code[3] && ids[p[2]].format!=ImageFormat::Unknown && p[1]<code[3]) {
            const uint32_t *ptr=ids[p[1]].definition;
            const uint32_t *im=ptr && ptr[0]==((4u<<16)|unsigned(Op::OpTypePointer)) && ptr[3]<code[3] ? ids[ptr[3]].definition : nullptr;
            if (im && im[0]==((9u<<16)|unsigned(Op::OpTypeImage)) && im[7]==2 && im[8]!=unsigned(ids[p[2]].format)) {
                uint32_t shape[7];
                memcpy(shape,im+2,sizeof(shape));
                shape[6]=unsigned(ids[p[2]].format);
                unsigned t=0;
                while(t<type_count && (memcmp(shape,types[t].shape,sizeof(shape)) || types[t].storage!=ptr[2])) ++t;
                if(t==type_count) {
                    auto &type=types[type_count++];
                    memcpy(type.shape,shape,sizeof(shape));
                    type.storage=ptr[2]; type.image=result[3]++; type.pointer=result[3]++;
                    result[used++]=im[0]; result[used++]=type.image;
                    memcpy(result+used,shape,sizeof(shape)); used+=7;
                    result[used++]=ptr[0]; result[used++]=type.pointer;
                    result[used++]=type.storage; result[used++]=type.image;
                }
                replacement=types[t].pointer;
                ids[p[2]].load_type=types[t].image;
            }
        } else if (op==Op::OpLoad && n>=4 && p[3]<code[3]) {
            replacement=ids[p[3]].load_type;
        }
        memcpy(result+used,p,n*4);
        if(replacement) result[used+1]=replacement;
        used+=n; i+=n;
    }
    VkShaderModuleCreateInfo typed=*info;
    typed.codeSize=used*4;
    typed.pCode=result;
    VkResult status=vkCreateShaderModule(device,type_count?&typed:info,allocator,module);
    free(types); free(result); free(ids);
    return status;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fsr_device_proc(VkDevice device,const char *name){
    if(!strcmp(name,"vkCreateShaderModule")) return (PFN_vkVoidFunction)fsr_shader_module;
    return vkGetDeviceProcAddr(device,name);
}
