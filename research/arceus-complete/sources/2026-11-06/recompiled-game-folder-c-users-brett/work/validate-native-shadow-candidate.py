import sys
sys.path.pop(0)
from pathlib import Path
import subprocess, json, shlex, hashlib
root=Path.cwd(); out=root/'work/native-shadow-candidate'
build=Path('C:/Users/brett/Documents/Codex/work/nvn-build')
compiler=Path('C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe')
s=(out/'render.c').read_text(); old=(out/'render.c.before').read_text()
def function(text, signature):
    a=text.index(signature); b=text.index('{',a); i=b+1; depth=1
    while depth:
        if text[i]=='{':depth+=1
        if text[i]=='}':depth-=1
        i+=1
    return text[a:i]
fixture=r'''
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
'''
fixture+=function(s,'static int native_shadow_shape(')+'\n'
fixture+=function(s,'static float target_raster_scale(')+'\n'
fixture+=function(s,'static int frame_consts(')+'\n'
fixture+=function(s,'static VkViewport render_viewport_scaled(')+'\n'
fixture+=function(old,'VkViewport vk_render_viewport_at(').replace('vk_render_viewport_at','original_viewport')+'\n'
fixture+=r'''
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
'''
(out/'offline-fixture.c').write_text(fixture)
def run(args):
    p=subprocess.run([str(a) for a in args],cwd=build,capture_output=True,text=True)
    assert p.returncode==0,p.stdout+p.stderr
    return p.stdout+p.stderr
log=run([compiler,'-std=c11','-O2','-Wall','-Wextra',out/'offline-fixture.c','-o',out/'offline-fixture.exe'])
log+=run([out/'offline-fixture.exe'])
(out/'offline-validation.txt').write_text(log)
print(log)
# Link only the compiler command, never the original post-link copy actions.
command=(root/'work/shadow-link-command.txt').read_text().split('cd . && ',1)[1].split(' && ',1)[0]
args=[x.strip('"') for x in shlex.split(command,posix=False)]
assert args[0].endswith('clang++.exe') and not any('&&' in x for x in args)
args[args.index('-o')+1]=str(out/'ArceusNativeShadow-runtime.exe')
args=[a for a in args if not a.startswith('-Wl,--out-implib,')]
args[1:1]=[str(out/(name+'.obj')) for name in ['surface.c','render.c','texture.c']]
(out/'link-arguments.json').write_text(json.dumps(args,indent=2))
(out/'link.txt').write_text(run(args))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
proof={
 'candidate_sha256':sha(out/'ArceusNativeShadow-runtime.exe'),
 'accepted_candidate_sha256':sha(root/'work/ArceusEngineCandidate-runtime.exe'),
 'clock_source_sha256':sha(Path('C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c')),
 'validation':log, 'launched_game':False,
 'scope':'Private objects linked before existing runtime archive; shared source, installed game, launchers unchanged.'}
(out/'validation-provenance.json').write_text(json.dumps(proof,indent=2))
print(json.dumps(proof,indent=2))
