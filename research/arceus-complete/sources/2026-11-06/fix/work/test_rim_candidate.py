from pathlib import Path
import subprocess,json
root=Path(__file__).resolve().parent.parent;out=root/'work/rim-candidate'
fixture=r'''
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
'''+(root/'work/rim_candidate.inc').read_text()+r'''
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
'''
(out/'fixture.c').write_text(fixture)
cc='C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe'
p=subprocess.run([cc,str(out/'fixture.c'),'-O2','-Wall','-Wextra','-o',str(out/'fixture.exe')],capture_output=True,text=True);assert p.returncode==0,p.stderr
cap=Path('C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/captures')
files=sorted(cap.glob('*1907*_light_0_cb3.bin'));assert len(files)==3
p=subprocess.run([str(out/'fixture.exe')]+list(map(str,files)),capture_output=True,text=True);assert p.returncode==0,p.stdout+p.stderr
(out/'validation.txt').write_text(p.stdout);print(p.stdout)
