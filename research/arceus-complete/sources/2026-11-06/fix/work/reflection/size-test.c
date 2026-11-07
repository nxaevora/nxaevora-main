#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t u32; typedef uint64_t u64; typedef int VkImageAspectFlags;
enum { VK_IMAGE_ASPECT_DEPTH_BIT=1,VK_IMAGE_ASPECT_COLOR_BIT=2,VK_IMAGE_LAYOUT_UNDEFINED=0,VK_IMAGE_VIEW_TYPE_2D=1,VK_IMAGE_VIEW_TYPE_2D_ARRAY=2,VK_IMAGE_VIEW_TYPE_CUBE=3,VK_IMAGE_VIEW_TYPE_CUBE_ARRAY=4,VK_IMAGE_VIEW_TYPE_3D=5 };
typedef struct { int img,is_rt,took_drawn,layout,pred_undefined,volume,type,depth3d; u32 sw,sh,layers;u64 last_used; struct {int depth,vk;} f;} Surf;
#define DIAG_SURF_IMAGE_BYTES (64ull<<20)
enum {VK_FORMAT_B10G11R11_UFLOAT_PACK32,VK_FORMAT_BC6H_SFLOAT_BLOCK,VK_FORMAT_BC6H_UFLOAT_BLOCK,VK_FORMAT_D16_UNORM,VK_FORMAT_D16_UNORM_S8_UINT,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D32_SFLOAT,VK_FORMAT_D32_SFLOAT_S8_UINT,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16G16_SNORM,VK_FORMAT_R16G16_UNORM,VK_FORMAT_R16_SFLOAT,VK_FORMAT_R16_UNORM,VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R32_SFLOAT,VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8_UINT,VK_FORMAT_R8_UNORM,VK_FORMAT_X8_D24_UNORM_PACK32};
static u32 reflection_block_bytes(const Surf* s, VkImageAspectFlags* aspect) {
    *aspect = s->f.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    if (s->f.depth) {
        switch (s->f.vk) {
            case VK_FORMAT_D16_UNORM:
            case VK_FORMAT_D16_UNORM_S8_UINT: return 2;
            case VK_FORMAT_X8_D24_UNORM_PACK32:
            case VK_FORMAT_D24_UNORM_S8_UINT:
            case VK_FORMAT_D32_SFLOAT:
            case VK_FORMAT_D32_SFLOAT_S8_UINT: return 4;
            default: return 0;
        }
    }
    /* Vulkan storage bytes, not f.bpp: converted guest textures can differ. No
       native compressed/packed-YUV images or guessed bytes-per-pixel. */
    switch (s->f.vk) {
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK: return 16;
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_UINT: return 1;
        case VK_FORMAT_R16_UNORM:
        case VK_FORMAT_R16_SFLOAT: return 2;
        case VK_FORMAT_R16G16_UNORM:
        case VK_FORMAT_R16G16_SNORM:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R32G32_SFLOAT: return 8;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
        default: return 0;
    }
}

static int reflection_surface_size(const Surf* s, u32 present, u32 mip, u32* bpp,
                                   VkImageAspectFlags* aspect, u64* size) {
    if (!s->img || (s->type != VK_IMAGE_VIEW_TYPE_CUBE && s->type != VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) ||
        (u32)(present - (u32)s->last_used) > 2 || !s->sw || !s->sh || !s->layers ||
        s->layout == VK_IMAGE_LAYOUT_UNDEFINED || s->pred_undefined || s->volume ||
        s->type == VK_IMAGE_VIEW_TYPE_3D || s->depth3d > 1) return 0;
    if (s->type != VK_IMAGE_VIEW_TYPE_2D && s->type != VK_IMAGE_VIEW_TYPE_2D_ARRAY &&
        s->type != VK_IMAGE_VIEW_TYPE_CUBE && s->type != VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) return 0;
    *bpp = reflection_block_bytes(s, aspect);
    if (!*bpp) return 0;
    /* Bound each multiply before doing it, including corrupt/unexpected extents. */
    u32 w = s->sw >> mip, h = s->sh >> mip;
    if (!w) w = 1; if (!h) h = 1;
    if (w > DIAG_SURF_IMAGE_BYTES || h > DIAG_SURF_IMAGE_BYTES) return 0;
    u32 block = s->f.vk == VK_FORMAT_BC6H_SFLOAT_BLOCK || s->f.vk == VK_FORMAT_BC6H_UFLOAT_BLOCK ? 4 : 1;
    w = (w + block - 1) / block; h = (h + block - 1) / block;
    u64 n = *bpp;
    if (w > DIAG_SURF_IMAGE_BYTES / n) return 0;
    n *= w;
    if (h > DIAG_SURF_IMAGE_BYTES / n) return 0;
    n *= h;
    if (s->layers > DIAG_SURF_IMAGE_BYTES / n) return 0;
    *size = n * s->layers;
    return 1;
}


int main(void) {
 Surf s={.img=1,.layout=1,.type=VK_IMAGE_VIEW_TYPE_CUBE,.depth3d=1,.sw=64,.sh=64,.layers=6,.last_used=100,.f.vk=VK_FORMAT_BC6H_SFLOAT_BLOCK};
 u32 b;u64 size,sum=0;VkImageAspectFlags a;
 u64 expected[]={24576,6144,1536,384,96,96,96};
 for(u32 m=0;m<7;m++){assert(reflection_surface_size(&s,100,m,&b,&a,&size));assert(b==16&&size==expected[m]);sum+=size;}
 assert(sum==32928);
 s.f.vk=VK_FORMAT_BC6H_UFLOAT_BLOCK;assert(reflection_surface_size(&s,100,6,&b,&a,&size)&&size==96);
 s.f.vk=VK_FORMAT_R16G16B16A16_SFLOAT;assert(reflection_surface_size(&s,100,0,&b,&a,&size)&&size==196608);
 s.type=VK_IMAGE_VIEW_TYPE_2D;assert(!reflection_surface_size(&s,100,0,&b,&a,&size));s.type=VK_IMAGE_VIEW_TYPE_CUBE;
 s.last_used=90;assert(!reflection_surface_size(&s,100,0,&b,&a,&size));s.last_used=100;
 s.layers=0;assert(!reflection_surface_size(&s,100,0,&b,&a,&size));s.layers=6;
 s.layout=0;assert(!reflection_surface_size(&s,100,0,&b,&a,&size));s.layout=1;
 s.sw=65536;s.sh=65536;assert(!reflection_surface_size(&s,100,0,&b,&a,&size));
 puts("PASS: actual C size helper: BC6 signed/unsigned, tiny mips, six faces, uncompressed, stale/noncube/undefined/oversize rejection");
}
