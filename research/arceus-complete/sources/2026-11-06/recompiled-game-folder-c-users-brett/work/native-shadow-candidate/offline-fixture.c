
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
typedef uint32_t u32; typedef uint64_t u64; typedef uint8_t u8;
typedef u64 VkBuffer;
typedef struct {u32 w,h,fkey,sw,sh;} Surf;
typedef struct {float x,y,width,height,minDepth,maxDepth;} VkViewport;
static float g_vk_scale;
#define GUEST_VIEWPORTS 16
#define LOG_LIMIT(...) ((void)0)
#define fatal(...) abort()
static float vk_scf(float x) {return x*g_vk_scale;}
static u64 submission=1; static unsigned allocations; static int fail_stage;
static _Alignas(16) u8 staging[16384];
static u64 vk_submit_number(void) {return submission;}
static u64 vk_stage_align(void) {return 16;}
static u8* vk_stage(u64 size,u64 align,VkBuffer* b,u64* off) {
    (void)align; if(fail_stage)return NULL;
    assert(size==64 && allocations<256); *b=1; *off=allocations++*64;
    return staging+*off;
}
static void snx_le32_array(void* p,int n) {(void)p;(void)n;}
static int native_shadow_shape(u32 w, u32 h, u32 fkey) {
    const char* flag = getenv("SNX_ARCEUS_NATIVE_SHADOWS");
    return flag && !strcmp(flag, "1") && w == 1024 && h == 1024 &&
           (fkey == 0x1000a || fkey == 0xf6);
}
static float target_raster_scale(Surf** rt, Surf* zt, u32 n) {
    int selected = 0, other = 0;
    for (u32 i = 0; i <= n; i++) {
        const Surf* t = i == n ? zt : rt[i];
        if (!t) continue;
        if (!native_shadow_shape(t->w, t->h, t->fkey) || t->sw != t->w || t->sh != t->h)
            other = 1;
        else selected = 1;
    }
    if (selected && other && g_vk_scale != 1.0f) {
        LOG_LIMIT(8, "arceus-native-shadow: mixed attachment scales; experimental pass skipped\n");
        return 0.0f;
    }
    return selected ? 1.0f : g_vk_scale;
}
static int frame_consts(VkBuffer* buf, u64* off, float raster_scale) {
    static u64 frame = ~0ull, o;
    static float cached_scale;
    static VkBuffer b;
    if (frame != vk_submit_number() || cached_scale != raster_scale) {
        u8* p = vk_stage(64, vk_stage_align(), &b, &o);
        if (!p) return 0;
        float rs[4] = {1 / raster_scale, 1 / raster_scale, raster_scale, 0}, fa[4] = {0, 0, 0, 1};
        u32 ia[4] = {0, 0, 0, 1};
        memcpy(p, rs, 16);
        memcpy(p + 16, fa, 16);
        memcpy(p + 32, ia, 16);
        memset(p + 48, 0, 16); /* empty vertex bindings retain their original format */
        snx_le32_array(p, 12); /* the device reads them little-endian */
        frame = vk_submit_number();
        cached_scale = raster_scale;
    }
    *buf = b;
    *off = o;
    return 1;
}
static VkViewport render_viewport_scaled(const u32* r, u32 index, float raster_scale) {
    if (index >= GUEST_VIEWPORTS) fatal("vulkan: viewport %u of the %u the GPU has", index, GUEST_VIEWPORTS);
    float vt[6];
    memcpy(vt, &r[0x280 + index * 8], sizeof vt);
    float sx = vt[0], sy = vt[1], tx = vt[3], ty = vt[4], ax = sx < 0 ? -sx : sx, ay = sy < 0 ? -sy : sy;
    VkViewport vp;
    int small = 2 * ax < 1 || 2 * ay < 1;
    if (small != !(r[0x64B] & 1))
        LOG_LIMIT(1, "vulkan: viewport scale %g x %g with transform enable %x\n", sx, sy, r[0x64B]);
    if (small) {
        u32 vx = r[0x300 + index * 4], vy = r[0x301 + index * 4];
        vp = (VkViewport){((float)(vx & 0xFFFF) * raster_scale), ((float)(vy & 0xFFFF) * raster_scale), ((float)(vx >> 16) * raster_scale), ((float)(vy >> 16) * raster_scale), 0, 1};
    } else
        vp = (VkViewport){((float)(tx - ax) * raster_scale), ((float)(ty - sy) * raster_scale), ((float)(2 * ax) * raster_scale), ((float)(2 * sy) * raster_scale), 0, 1};
    /* A lower-left window measures y from the surface clip's bottom. Match the 3D blit path's conversion. */
    if (r[0x4EB] & 1) {
        vp.y += ((float)((float)(r[0x3FE] >> 16)) * raster_scale);
        vp.height = -vp.height;
    }
    if (((r[0x286 + index * 8] >> 4) & 7) == 3) {
        vp.y += vp.height;
        vp.height = -vp.height;
    }
    float zn, zf;
    memcpy(&zn, &r[0x302 + index * 4], 4);
    memcpy(&zf, &r[0x303 + index * 4], 4);
    if (vt[2] != 0.0f || vt[5] != 0.0f) {
        zn = vt[5] - ((r[0x35F] & 1) ? 0.0f : vt[2]);
        zf = vt[5] + vt[2];
    }
    vp.minDepth = zn < 0 ? 0 : zn > 1 ? 1 : zn;
    vp.maxDepth = zf < 0 ? 0 : zf > 1 ? 1 : zf;
    return vp;
}
VkViewport original_viewport(const u32* r, u32 index) {
    if (index >= GUEST_VIEWPORTS) fatal("vulkan: viewport %u of the %u the GPU has", index, GUEST_VIEWPORTS);
    float vt[6];
    memcpy(vt, &r[0x280 + index * 8], sizeof vt);
    float sx = vt[0], sy = vt[1], tx = vt[3], ty = vt[4], ax = sx < 0 ? -sx : sx, ay = sy < 0 ? -sy : sy;
    VkViewport vp;
    int small = 2 * ax < 1 || 2 * ay < 1;
    if (small != !(r[0x64B] & 1))
        LOG_LIMIT(1, "vulkan: viewport scale %g x %g with transform enable %x\n", sx, sy, r[0x64B]);
    if (small) {
        u32 vx = r[0x300 + index * 4], vy = r[0x301 + index * 4];
        vp = (VkViewport){vk_scf(vx & 0xFFFF), vk_scf(vy & 0xFFFF), vk_scf(vx >> 16), vk_scf(vy >> 16), 0, 1};
    } else
        vp = (VkViewport){vk_scf(tx - ax), vk_scf(ty - sy), vk_scf(2 * ax), vk_scf(2 * sy), 0, 1};
    /* A lower-left window measures y from the surface clip's bottom. Match the 3D blit path's conversion. */
    if (r[0x4EB] & 1) {
        vp.y += vk_scf((float)(r[0x3FE] >> 16));
        vp.height = -vp.height;
    }
    if (((r[0x286 + index * 8] >> 4) & 7) == 3) {
        vp.y += vp.height;
        vp.height = -vp.height;
    }
    float zn, zf;
    memcpy(&zn, &r[0x302 + index * 4], 4);
    memcpy(&zf, &r[0x303 + index * 4], 4);
    if (vt[2] != 0.0f || vt[5] != 0.0f) {
        zn = vt[5] - ((r[0x35F] & 1) ? 0.0f : vt[2]);
        zf = vt[5] + vt[2];
    }
    vp.minDepth = zn < 0 ? 0 : zn > 1 ? 1 : zn;
    vp.maxDepth = zf < 0 ? 0 : zf > 1 ? 1 : zf;
    return vp;
}

int main(void) {
    unsigned cases=0;
    Surf depth={1024,1024,0x1000a,1024,1024}, tag={1024,1024,0xf6,1024,1024};
    Surf scene={1920,1080,0x1000b,2560,1440}; Surf* rt[8]={&tag};
    g_vk_scale=4.f/3.f;
    _putenv_s("SNX_ARCEUS_NATIVE_SHADOWS", "0");
    assert(target_raster_scale(rt,&depth,1)==g_vk_scale); cases++;
    _putenv_s("SNX_ARCEUS_NATIVE_SHADOWS", "1");
    assert(target_raster_scale(rt,&depth,1)==1); cases++;
    assert(target_raster_scale(rt,NULL,1)==1); cases++;
    assert(target_raster_scale(rt,&depth,0)==1); cases++;
    assert(target_raster_scale(rt,NULL,0)==g_vk_scale); cases++;
    rt[0]=&scene; assert(target_raster_scale(rt,&depth,1)==0); cases++;
    assert(target_raster_scale(rt,NULL,1)==g_vk_scale); cases++;
    for(u32 f=0;f<0x10010;f++) {
        assert(native_shadow_shape(1024,1024,f)==(f==0xf6 || f==0x1000a));cases++;
    }
    assert(!native_shadow_shape(1024,1023,0x1000a));cases++;
    assert(!native_shadow_shape(1920,1080,0xf6));cases++;
    float scales[]={1,4.f/3.f,2};
    for(unsigned scale=0;scale<3;scale++) for(u32 index=0;index<16;index++)
    for(u32 origin=0;origin<2;origin++) for(u32 swizzle=0;swizzle<2;swizzle++)
    for(u32 small=0;small<2;small++) for(u32 neg=0;neg<2;neg++) {
        u32 r[0x1000]={0};
        float vt[]={small?0:512,small?0:(neg?-512:512),.5,512,512,.5};
        memcpy(&r[0x280+index*8],vt,sizeof vt);
        r[0x286+index*8]=swizzle?0x30:0;
        r[0x300+index*4]=(1024<<16)|7; r[0x301+index*4]=(1024<<16)|11;
        r[0x4EB]=origin; r[0x3FE]=1024<<16; r[0x64B]=!small;
        g_vk_scale=scales[scale];
        VkViewport a=original_viewport(r,index), b=render_viewport_scaled(r,index,g_vk_scale);
        assert(!memcmp(&a,&b,sizeof a));cases++;
        g_vk_scale=1; a=original_viewport(r,index);g_vk_scale=scales[scale];
        b=render_viewport_scaled(r,index,1);
        assert(!memcmp(&a,&b,sizeof a));cases++;
    }
    VkBuffer b;u64 o,last;float values[]={4.f/3.f,4.f/3.f,1,1,4.f/3.f,1,2};
    unsigned expected=0; float previous=0;
    for(unsigned i=0;i<7;i++) {
        if(previous!=values[i])expected++;
        assert(frame_consts(&b,&o,values[i]));
        assert(allocations==expected);
        float* f=(float*)(staging+o);u32* u=(u32*)(staging+o);
        assert(f[0]==1/values[i] && f[1]==1/values[i] && f[2]==values[i] && f[3]==0);
        assert(f[4]==0 && f[7]==1 && u[8]==0 && u[11]==1 && u[12]==0 && u[15]==0);
        previous=values[i];cases++;
    }
    last=o; submission++;assert(frame_consts(&b,&o,2));assert(o!=last);cases++;
    fail_stage=1;assert(!frame_consts(&b,&o,1));cases++;
    fail_stage=0;assert(frame_consts(&b,&o,1));assert(((float*)(staging+o))[0]==1);cases++;
    printf("%u offline C assertions groups passed; viewport comparison and constant-cache transitions covered.\n",cases);
    return 0;
}
