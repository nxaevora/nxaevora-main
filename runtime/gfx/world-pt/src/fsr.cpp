// AMD FSR 3.1's upscaler for exec.c's temporal anti-aliasing (F1 > Rendering > Temporal anti-aliasing > AMD FSR 3.1):
// the same frame as FSR 2's (vk_fsr2.h VkFsr2Frame), through AMD's signed DLLs (third_party/ffx_api/README.ZARecomp.md)
// loaded from next to the exe the first time it is used. Windows only; without the DLLs, or once its context failed,
// vk_fsr3_available is false and exec.c uses FSR 2. The jitter is FSR 2's (the same Halton sequence and phase count).
// The DLL picks the newest upscaler the GPU runs (FSR 4 on the Radeons that have it, FSR 3.1 elsewhere): logged.
#include <float.h>
#include <cstring>
#include "vk_fsr2.h"
#ifdef _WIN32
#include <windows.h>
#include "ffx_api.h"
#include "ffx_api_vk.h"
#include "ffx_upscale.h"

#define LOG vk_fsr_log

static PfnFfxCreateContext p_create;
static PfnFfxDestroyContext p_destroy;
static PfnFfxQuery p_query;
static PfnFfxDispatch p_dispatch;
static int loaded;  // (0 not tried, 1 loaded, -1 missing: not tried again)
static ffxContext ctx;
static bool have, failed;  // (failed: its context couldn't be made - FSR 2 for the rest of the session)
static uint32_t cur_ow, cur_oh;

#include "fsr-formats.inc"
static bool load(void) {
    if (loaded) return loaded > 0;
    loaded = -1;
    HMODULE m = LoadLibraryW(L"amd_fidelityfx_loader_vk.dll");
    if (!m) { LOG("[fsr3] amd_fidelityfx_loader_vk.dll not found: FSR 2 instead\n"); return false; }
    p_create = (PfnFfxCreateContext)(void *)GetProcAddress(m, "ffxCreateContext");
    p_destroy = (PfnFfxDestroyContext)(void *)GetProcAddress(m, "ffxDestroyContext");
    p_query = (PfnFfxQuery)(void *)GetProcAddress(m, "ffxQuery");
    p_dispatch = (PfnFfxDispatch)(void *)GetProcAddress(m, "ffxDispatch");
    if (!p_create || !p_destroy || !p_query || !p_dispatch) { LOG("[fsr3] the loader DLL lacks its functions\n"); return false; }
    loaded = 1;
    return true;
}

static void message(uint32_t type, const wchar_t *m) { vk_fsr_message("fsr3", type == FFX_API_MESSAGE_TYPE_ERROR, m); }

bool vk_fsr3_available(void) { return !failed && load(); }

static void destroy(VkDevice dev) {
    if (!have) return;
    vkDeviceWaitIdle(dev);
    p_destroy(&ctx, NULL);
    have = false;
}

// (the images stay in VK_IMAGE_LAYOUT_GENERAL: GENERIC_READ / UNORDERED_ACCESS are the states the SDK's Vulkan backend
// maps to it, and it puts every resource back in the state it was given)
static FfxApiResource res(VkImage img, VkFormat fmt, uint32_t w, uint32_t h, uint32_t state, uint32_t usage) {
    FfxApiResourceDescription d = {};
    d.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    d.format = ffxApiGetSurfaceFormatVK(fmt);
    d.width = w, d.height = h, d.depth = 1, d.mipCount = 1;
    d.usage = usage | (ffxApiIsDepthFormat(fmt) ? FFX_API_RESOURCE_USAGE_DEPTHTARGET : 0);
    return ffxApiGetResourceVK((void *)img, d, state);
}

bool vk_fsr3_record(VkPhysicalDevice phys, VkDevice dev, VkCommandBuffer cmd, const VkFsr2Frame *f) {
    if (!vk_fsr3_available()) return false;
    if (!have || f->ow != cur_ow || f->oh != cur_oh) {
        destroy(dev);
        cur_ow = f->ow, cur_oh = f->oh;
        failed = true;
        fsr_output_format = f->out_format == VK_FORMAT_R16G16B16A16_SFLOAT ? ImageFormat::Rgba16f : ImageFormat::Unknown;
        ffxCreateBackendVKDesc be = {};
        be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        be.deviceContext = {dev, phys, fsr_device_proc};
        ffxCreateContextDescUpscale d = {};
        d.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        d.header.pNext = &be.header;
        d.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;
        d.maxRenderSize = {f->w, f->h};
        d.maxUpscaleSize = {f->ow, f->oh};
        d.fpMessage = message;
        ffxQueryDescGetVersions q={}; q.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS; q.createDescType=d.header.type; q.device=&be.deviceContext;
        uint64_t ids[32],count=32; const char *names[32]={}; q.outputCount=&count;q.versionIds=ids;q.versionNames=names;
        if(p_query(nullptr,&q.header)!=FFX_API_RETURN_OK||count>32)return false;
        ffxOverrideVersion version={};version.header.type=FFX_API_DESC_TYPE_OVERRIDE_VERSION;
        for(unsigned i=0;i<count;i++){LOG("[fsr3] provider: %s\n",names[i]?names[i]:"?");if(names[i]&&strstr(names[i],"3.1.6"))version.versionId=ids[i];}
        if(!version.versionId)return false;be.header.pNext=&version.header;
        ffxReturnCode_t e = p_create(&ctx, &d.header, NULL);
        if (e != FFX_API_RETURN_OK) { LOG("[fsr3] context failed: %u\n", e); return false; }
        have = true, failed = false;
        ffxQueryGetProviderVersion v = {};
        v.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        p_query(&ctx, &v.header);
        LOG("[fsr3] context %ux%u: %s\n", f->ow, f->oh, v.versionName ? v.versionName : "?");
    }
    ffxDispatchDescUpscale d = {};
    d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    d.commandList = (void *)cmd;
    const uint32_t rd = FFX_API_RESOURCE_STATE_COMPUTE_READ;
    d.color = res(f->color, f->color_format, f->w, f->h, rd, 0);
    d.depth = res(f->depth, f->depth_format, f->w, f->h, rd, 0);
    d.motionVectors = res(f->mv, VK_FORMAT_R16G16_SFLOAT, f->w, f->h, rd, 0);
    if(f->reactive) d.reactive = res(f->reactive, VK_FORMAT_R8_UNORM, f->w, f->h, rd, 0);
    d.output = res(f->out, f->out_format, f->ow, f->oh, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, FFX_API_RESOURCE_USAGE_UAV);
    d.jitterOffset = {f->jx, f->jy};
    d.motionVectorScale = {1, 1};  // (pixels)
    d.renderSize = {f->w, f->h};
    d.upscaleSize = {f->ow, f->oh};
    d.enableSharpening = f->sharpness > 0;
    d.sharpness = f->sharpness;
    d.frameTimeDelta = f->dt_ms;
    d.preExposure = 1;
    d.reset = f->reset;
    d.cameraNear = f->near_z;
    d.cameraFar = f->far_z;
    d.cameraFovAngleVertical = f->fov_y;
    d.viewSpaceToMetersFactor = 1;
    ffxReturnCode_t e = p_dispatch(&ctx, &d.header);
    if (e != FFX_API_RETURN_OK) {
        static int n;
        if (n++ < 10) LOG("[fsr3] dispatch failed: %u\n", e);
        return false;
    }
    return true;
}
#else
bool vk_fsr3_available(void) { return false; }
bool vk_fsr3_record(VkPhysicalDevice, VkDevice, VkCommandBuffer, const VkFsr2Frame *) { return false; }
#endif
