
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <math.h>
typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64; typedef u64 VkBuffer;
typedef struct { u64 cbind[5][18]; } GpuChannel;
typedef struct { u64 hash; } VkSh;
static u8 source[65536],staging[65536];static int gpu_pending,allocation_fail,allocations;
static const char* fixture_getenv(const char* unused){(void)unused;return "1";}
#define getenv fixture_getenv
#define LOG_LIMIT(...) ((void)0)
static int vk_buf_pending(u64 a,u32 n){(void)a;(void)n;return gpu_pending;}
static int vk_compute_gpu_src(u64 a,u32 n,VkBuffer*b,u64*o){(void)a;(void)n;(void)b;(void)o;return 0;}
static const void* gmem_ptr(u64 a,u32 n){(void)a;(void)n;return source;}
static u32 vk_stage_align(void){return 256;}
static void* vk_stage(u32 n,u32 align,VkBuffer*b,u64*o){assert(n<=65536&&align==256);if(allocation_fail)return NULL;allocations++;*b=999;*o=256;return staging;}
/* Opt-in cosmetic workaround, not a proven emulation correction.
   Match the captured scene's two category1 color entries exactly. */
static int rim_candidate_enabled(void) {
    static int on = -1;
    if (on < 0) { const char* e = getenv("SNX_ARCEUS_NEUTRAL_RIM"); on = e && !strcmp(e,"1"); }
    return on;
}
static int rim_candidate_match(const void* data, u32 size, int stage, int bank, u64 hash) {
    static const u32 purple[4] = {0x3ee9aeb6,0x3e405416,0x3f3ce7b4,0x3ef0f0f1};
    static const u32 warm[4] = {0x3f800000,0x3f4a9282,0x3f54ad57,0x3f169697};
    if (!data || size < 0x160 || size > 65536 || stage != 4 || bank != 3 ||
        (hash != 0x89619027ba43e5d3ull && hash != 0xe02c1a109a77fac4ull)) return 0;
    return !memcmp((const u8*)data+0x150,purple,sizeof purple) &&
           !memcmp((const u8*)data+0xd0,warm,sizeof warm);
}
static void rim_candidate_apply(void* data) {
    float rgb[3]; memcpy(rgb,(u8*)data+0x150,sizeof rgb);
    float y = .2126f*rgb[0] + .7152f*rgb[1] + .0722f*rgb[2];
    rgb[0]=rgb[1]=rgb[2]=y;
    memcpy((u8*)data+0x150,rgb,sizeof rgb);
}
static void rim_candidate_buffer(GpuChannel* ch, int stage, int bank, VkSh* shader,
                                  u32 size, VkBuffer* buffer, u64* offset) {
    if (!rim_candidate_enabled() || stage!=4 || bank!=3 ||
        (shader->hash!=0x89619027ba43e5d3ull && shader->hash!=0xe02c1a109a77fac4ull)) return;
    u64 address=ch->cbind[stage][bank]; VkBuffer pending; u64 pending_offset;
    if (!address || vk_buf_pending(address,size) || vk_compute_gpu_src(address,size,&pending,&pending_offset)) {
        LOG_LIMIT(4,"arceus-neutral-rim: skipped non-CPU constant buffer\n"); return;
    }
    const void* src=gmem_ptr(address,size);
    if (!rim_candidate_match(src,size,stage,bank,shader->hash)) return;
    VkBuffer b; u64 o;
    void* dst=vk_stage(size,vk_stage_align(),&b,&o);
    if (!dst) { LOG_LIMIT(4,"arceus-neutral-rim: allocation failed; original lighting retained\n"); return; }
    memcpy(dst,src,size); rim_candidate_apply(dst);
    *buffer=b; *offset=o;
    LOG_LIMIT(8,"arceus-neutral-rim: applied category1 purple tint workaround fs=%llx; alpha and luminance preserved\n",(unsigned long long)shader->hash);
}

int main(int argc,char**argv){
 assert(argc>1);
 for(int file=1;file<argc;file++){
  FILE*f=fopen(argv[file],"rb");assert(f);size_t n=fread(source,1,sizeof source,f);assert(!ferror(f)&&feof(f));fclose(f);
  u8 original[65536];memcpy(original,source,n);
  assert(rim_candidate_match(source,(u32)n,4,3,0x89619027ba43e5d3ull));
  assert(rim_candidate_match(source,(u32)n,4,3,0xe02c1a109a77fac4ull));
  assert(!rim_candidate_match(source,0x15f,4,3,0x89619027ba43e5d3ull));
  assert(!rim_candidate_match(source,(u32)n,0,3,0x89619027ba43e5d3ull));
  assert(!rim_candidate_match(source,(u32)n,4,4,0x89619027ba43e5d3ull));
  assert(!rim_candidate_match(source,(u32)n,4,3,1));
  source[0x150]^=1;assert(!rim_candidate_match(source,(u32)n,4,3,0x89619027ba43e5d3ull));source[0x150]^=1;
  source[0xd0]^=1;assert(!rim_candidate_match(source,(u32)n,4,3,0x89619027ba43e5d3ull));source[0xd0]^=1;
  GpuChannel ch={0};ch.cbind[4][3]=1;VkSh shader={0x89619027ba43e5d3ull};VkBuffer b=7;u64 o=8;
  int before=allocations;gpu_pending=1;rim_candidate_buffer(&ch,4,3,&shader,(u32)n,&b,&o);assert(b==7&&o==8&&allocations==before);gpu_pending=0;
  allocation_fail=1;rim_candidate_buffer(&ch,4,3,&shader,(u32)n,&b,&o);assert(b==7&&o==8&&allocations==before);allocation_fail=0;
  rim_candidate_buffer(&ch,4,3,&shader,(u32)n,&b,&o);assert(b==999&&o==256&&allocations==before+1);
  assert(!memcmp(original,source,n));assert(!memcmp(source,staging,0x150));assert(!memcmp(source+0x15c,staging+0x15c,n-0x15c));
  float a[3],z[3];memcpy(a,source+0x150,12);memcpy(z,staging+0x150,12);
  float y=.2126f*a[0]+.7152f*a[1]+.0722f*a[2];assert(z[0]==z[1]&&z[1]==z[2]);assert(fabsf(y-z[0])<1e-7f);
  printf("PASS capture %d: exact match gating, only12 RGB bytes modified, alpha/other constants/source retained, luminance %.8f, pending/allocation failure leave original binding\n",file,y);
 }
}
