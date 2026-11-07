/* ------------------------------------------------------------ samplers */
static VkSamplerAddressMode wrap(u32 m) {
    switch (m) {
        case 0: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case 1: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case 2:
        case 4: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case 3:
        case 6: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case 5:
        case 7:
            /* an optional feature: without it mirrored repeat, the same over the first mirror */
            return g_vk.has_mirror_clamp ? VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE
                                         : VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

/* tsc[0] bit 9 compare, bits 10-12 the compare function; tsc[1] filters (gpu/vk/render.c forces nearest for integer
   textures and the compare mode the shader declares) */
int g_vk_anisotropy;
VkSampler vk_sampler(const u32* tsc, int raise, int integer) {
    /* samplers are kept for good, and a device allows at least 4000 (maxSamplerAllocationCount): past 3072, a quarter
       of the table left free for probing, new sampler states get the first sampler made */
    enum { TAB = 4096, MAX = 3072 };
    static struct {
        u32 tsc[8];
        int raise, integer;
        VkSampler s;
    } tab[TAB];
    static u32 n, ncustom;
    static VkSampler first;
    u32 hk = 2166136261u;
    for (int i = 0; i < 8; i++) hk = (hk ^ tsc[i]) * 16777619u;
    hk = (hk ^ (u32)(raise << 1 | integer)) * 16777619u;
    u32 j = hk & (TAB - 1);
    for (u32 probe = 0; probe < TAB && tab[j].s; probe++, j = (j + 1) & (TAB - 1))
        if (!memcmp(tab[j].tsc, tsc, 32) && tab[j].raise == raise && tab[j].integer == integer) return tab[j].s;
    if (n >= MAX || tab[j].s) {
        LOG_LIMIT(1, "vulkan: more than %d sampler states: the others use the first one\n", MAX);
        return first;
    }
    u32 mag = tsc[1] & 3, min = (tsc[1] >> 4) & 3, mip = (tsc[1] >> 6) & 3;
    float minl = (float)(tsc[2] & 0xFFF) / 256.f, maxl = (float)((tsc[2] >> 12) & 0xFFF) / 256.f;
    i32 bias = (i32)((tsc[1] >> 12) & 0x1FFF);
    if (bias & 0x1000) bias |= ~0x1FFF;
    float lb = (float)bias / 256.f, lbmax = g_vk.props.limits.maxSamplerLodBias;
    if (lb > lbmax) lb = lbmax;
    if (lb < -lbmax) lb = -lbmax;
    static const float aniso_lut[8] = {1, 2, 4, 6, 8, 10, 12, 16}; /* Ryujinx's SamplerDescriptor */
    u32 aniso = (tsc[0] >> 20) & 7;
    float af = aniso_lut[aniso];
    /* anisotropy=N raises a mipmapped 2D texture's linear filtering (raise), a 3D scene's surfaces, to N. Nearest
       filtering (2D art, the HUD), samplers without a mip filter and depth compares (shadow maps) keep the game's */
    if (raise && (float)g_vk_anisotropy > af && min == 2 && mag == 2 && mip >= 2 && !((tsc[0] >> 9) & 1))
        af = (float)g_vk_anisotropy;
    /* tsc[1] bits 10-11: the least (1) or the greatest (2) of the texels the filter reads, not their weighted average
       (0), which vk_tex_bind_sampler leaves only where the view's format and the device take it */
    u32 reduction = (tsc[1] >> 10) & 3;
    if (reduction == 3) {
        LOG_LIMIT(1, "vulkan: sampler reduction 3 names no mode: the texels are averaged\n");
        reduction = 0;
    }
    VkSamplerReductionModeCreateInfo rm = {.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
                                           .reductionMode = reduction == 1 ? VK_SAMPLER_REDUCTION_MODE_MIN
                                                                           : VK_SAMPLER_REDUCTION_MODE_MAX};
    VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                              .magFilter = mag == 2 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
                              .minFilter = min == 2 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
                              .mipmapMode = mip == 3 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
                              .addressModeU = wrap(tsc[0] & 7),
                              .addressModeV = wrap((tsc[0] >> 3) & 7),
                              .addressModeW = wrap((tsc[0] >> 6) & 7),
                              .mipLodBias = lb,
                              .anisotropyEnable = af > 1 && g_vk.feat.samplerAnisotropy,
                              .maxAnisotropy = af,
                              .compareEnable = (tsc[0] >> 9) & 1,
                              .compareOp = (VkCompareOp)((tsc[0] >> 10) & 7),
                              .minLod = minl,
                              .maxLod = mip <= 1 ? minl + 0.25f : maxl, /* no mip filter: the base level only */
                              .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK}; /* set below */
    if (si.maxAnisotropy > g_vk.props.limits.maxSamplerAnisotropy)
        si.maxAnisotropy = g_vk.props.limits.maxSamplerAnisotropy;
    if (si.maxLod < si.minLod) si.maxLod = si.minLod;
    /* the border color: the TSC's four words as the texture's texels read, floats for a float view and integers for an
       integer one (nouveau writes the integer ones as their bits), as Vulkan's FLOAT or INT colors. A custom color
       takes one of the device's few custom-color samplers while there are any left, else the nearest built-in one */
    float bc[4];
    i32 bi[4];
    memcpy(bc, &tsc[4], 16);
    memcpy(bi, &tsc[4], 16);
    int black = integer ? !bi[0] && !bi[1] && !bi[2] : bc[0] == 0 && bc[1] == 0 && bc[2] == 0;
    int white = integer ? bi[0] == 1 && bi[1] == 1 && bi[2] == 1 : bc[0] == 1 && bc[1] == 1 && bc[2] == 1;
    int clear = integer ? !bi[3] : bc[3] == 0, opaque = integer ? bi[3] == 1 : bc[3] == 1;
    int custom = !((black && (clear || opaque)) || (white && opaque));
    VkSamplerCustomBorderColorCreateInfoEXT cb = {.sType =
                                                      VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
    if (custom && (!g_vk.has_border_color || ncustom >= g_vk.border_color_max)) {
        float a = integer ? (float)bi[3] : bc[3];
        float lum = integer ? ((float)bi[0] + (float)bi[1] + (float)bi[2]) / 3 : (bc[0] + bc[1] + bc[2]) / 3;
        if (g_vk.has_border_color)
            LOG_LIMIT(1,
                      "vulkan: more than %u samplers with a border color of their own: the others take the nearest "
                      "of black or white\n",
                      g_vk.border_color_max);
        custom = 0;
        clear = !(a >= 0.5f); /* NaN: transparent, and black */
        black = !(lum >= 0.5f);
    }
    if (custom) {
        if (integer)
            memcpy(cb.customBorderColor.int32, bi, 16);
        else
            memcpy(cb.customBorderColor.float32, bc, 16);
        si.borderColor = integer ? VK_BORDER_COLOR_INT_CUSTOM_EXT : VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
        si.pNext = &cb;
    } else if (clear)
        si.borderColor = integer ? VK_BORDER_COLOR_INT_TRANSPARENT_BLACK : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    else if (black)
        si.borderColor = integer ? VK_BORDER_COLOR_INT_OPAQUE_BLACK : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    else
        si.borderColor = integer ? VK_BORDER_COLOR_INT_OPAQUE_WHITE : VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (reduction) {
        rm.pNext = si.pNext;
        si.pNext = &rm;
    }
    VkSampler s;
    if (!VK_OK(vkCreateSampler(g_vk.dev, &si, NULL, &s))) return VK_NULL_HANDLE;
    memcpy(tab[j].tsc, tsc, 32);
    tab[j].raise = raise;
    tab[j].integer = integer;
    tab[j].s = s;
    ncustom += custom;
    if (!n++) first = s;
    return s;
}

