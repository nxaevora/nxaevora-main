#include "common/cpu/recompiler/llvm/jit_service.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ui-capture-fixtures.h"
#define MEMORY 0x20000
#define LO 0x1000
#define HI 0x5000
#define OBJ 0x9000
static u8 memory[MEMORY];
static GuestFn dispatch[(HI-LO)/4];
static SnxJitService* service;
static RtAPI rt;
static unsigned cases,failures;
static void check(int ok,const char* why){cases++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",why);}}
static void put32(u64 p,u32 v){memcpy(memory+p,&v,4);}
static void put64(u64 p,u64 v){memcpy(memory+p,&v,8);}
static u32 get32(u64 p){u32 v;memcpy(&v,memory+p,4);return v;}
static void putf(u64 p,float f){memcpy(memory+p,&f,4);}
static float getf(u64 p){float f;memcpy(&f,memory+p,4);return f;}
static void miss(CPU* c,u64 pc){fprintf(stderr,"unexpected external call %llx\n",(unsigned long long)pc);failures++;c->pc=0;}
static void undef(CPU* c,u64 pc,u32 w){fprintf(stderr,"undefined %08x at %llx\n",w,(unsigned long long)pc);failures++;c->pc=0;}
static void poll(CPU* c){c->poll=0;}
static void report(const SnxJitReport* r){if(r->error)fprintf(stderr,"JIT: %s\n",r->error);}
static CPU cpu(void){CPU c={0};c.mem=memory;c.rt=&rt;c.disp=dispatch;c.code_lo=LO;c.code_hi=HI;c.x[31]=0x1f000;return c;}
int main(void){
 memcpy(memory+0x1000,fade_code,sizeof fade_code);
 memcpy(memory+0x3000,capture_wait_code,sizeof capture_wait_code);
 memcpy(memory+0x4000,ui_clock_code,sizeof ui_clock_code);
 put32(0x4800,0xaa0003f6);put32(0x4804,0xd65f03c0);
 put32(0x4820,0x91000016);put32(0x4824,0xd65f03c0);
 memcpy(memory+0x8000,ui_denominators,sizeof ui_denominators);
 // Restrict discovery to the actual code blocks; helper branches retain their
 // original relative positions in the unchanged contiguous fade block.
 SnxCodeImage images[4]={0};
 const u64 starts[]={0x1000,0x3000,0x4000,0x4800};
 const size_t lengths[]={sizeof fade_code,sizeof capture_wait_code,sizeof ui_clock_code,0x28};
 for(int i=0;i<4;i++){images[i].image=memory;images[i].image_size=MEMORY;images[i].text_off=starts[i];images[i].text_size=lengths[i];}
 SnxJitConfig config={0};config.images=images;config.image_count=4;config.dispatch=dispatch;config.code_lo=LO;config.code_hi=HI;config.threads=1;config.opt='2';config.report=report;
 rt.miss=miss;rt.undef=undef;rt.poll=poll;char error[512]={0};
 service=snx_jit_service_new(&config,error,sizeof error);
 if(!service){fprintf(stderr,"startup %s\n",error);return 2;}
 GuestFn fade=snx_jit_wait(service,0x1000),capture=snx_jit_wait(service,0x3000),clock=snx_jit_wait(service,0x4000);
 if(!fade||!capture||!clock){snx_jit_service_free(service);return 2;}
 const int durations[]={0,1,2,10,30,60,120};
 const float target[]={16,32,64,255};
 for(int mode=0;mode<=2;mode+=2)for(int d=0;d<7;d++){
    int duration=durations[d];memset(memory+OBJ,0,0x600);
    put32(OBJ+0x500,mode);put32(OBJ+0x504,3);put32(OBJ+0x53c,duration);put32(OBJ+0x518,~0u);
    for(int v=0;v<4;v++){putf(OBJ+0x550+v*4,duration?target[v]/duration:0);putf(OBJ+0x560+v*4,target[v]);}
    unsigned count=0;
    while(get32(OBJ+0x504)==3 && count<200){
        CPU c=cpu();c.x[0]=OBJ;fade(&c,0x1000);count++;
        check(c.pc==0&&c.x[31]==0x1f000,"full fade update returns with balanced stack");
        if(count<=(unsigned)duration){
            check(get32(OBJ+0x538)==count,"fade counter increments once per update");
            float expected=target[3]*(float)count/(duration?duration:1);
            check(fabsf(getf(OBJ+0x54c)-expected)<0.001f,"fade alpha follows configured per-step increment");
        }
    }
    check(count==(unsigned)duration+1&&get32(OBJ+0x504)==1,"state-3 fade completion callback and state");
    check(get32(OBJ+0x538)==0&&get32(OBJ+0x53c)==0,"fade completion resets clock pair");
    check(getf(OBJ+0x54c)==target[3],"fade completion snaps to target alpha");
    CPU c=cpu();c.x[0]=OBJ;fade(&c,0x1000);
    check(get32(OBJ+0x504)==1&&getf(OBJ+0x54c)==target[3],"completed fade remains idle");
    printf("fade mode=%d authored_frames=%d callbacks_to_idle=%u\n",mode,duration,count);
 }
 for(unsigned remaining=0;remaining<=120;remaining++){
    put32(OBJ+0x1dc,remaining);CPU c=cpu();c.x[20]=OBJ;capture(&c,0x3000);
    check(get32(OBJ+0x1dc)==(remaining?remaining-1:0),"capture wait decrements and preserves zero");
    check(c.pc==0,"capture wait exits at guarded continuation");
 }
 const u64 elapsed[]={0,8333333,16666667,33333334,7300000,100000000};
 for(unsigned mode=0;mode<5;mode++)for(unsigned t=0;t<6;t++){
    put64(0x8108,elapsed[t]);CPU c=cpu();c.x[0]=0x8100;c.x[8]=0x8000;c.x[21]=mode;clock(&c,0x4000);
    float expected=(float)((double)elapsed[t]/(double)(ui_denominators[mode]-1));
    check(c.v[8].f[0]==expected,"UI animation elapsed-time normalization");
 }
 const unsigned fps[]={30,60,120};
 for(unsigned i=0;i<3;i++){
    unsigned hz=fps[i];double total=0;
    for(unsigned frame=0;frame<hz;frame++){
        u64 before=(u64)frame*1000000000/hz,after=(u64)(frame+1)*1000000000/hz;
        put64(0x8108,after-before);CPU c=cpu();c.x[0]=0x8100;c.x[8]=0x8000;c.x[21]=1;clock(&c,0x4000);total+=c.v[8].f[0];
    }
    check(fabs(total-30)<0.00001,"UI clock preserves 30 authored units per second across rates");
    printf("UI assumed_update_hz=%u normalized_units_in_one_second=%.9f\n",hz,total);
 }
 GuestFn guard_before=snx_jit_wait(service,0x4800),guard_after=snx_jit_wait(service,0x4820);
 if(!guard_before||!guard_after){failures++;}else{
    const u64 inputs[]={0,1,0xffffffff,0x100000000ull,0x8000000000000000ull,~0ull};
    for(int i=0;i<6;i++)for(int flags=0;flags<16;flags++){
        CPU a=cpu();a.x[0]=inputs[i];set_nzcv(&a,flags);CPU b=a;
        guard_before(&a,0x4800);guard_after(&b,0x4820);
        check(!memcmp(a.x,b.x,sizeof a.x)&&get_nzcv(&a)==get_nzcv(&b)&&a.pc==b.pc,"capture MOV X22 / ADD X22 guard preserves registers and NZCV");
    }
 }
 SnxJitStats stats;snx_jit_stats(service,&stats);snx_jit_service_free(service);
 printf("UI/capture guest checks=%u failures=%u compiled_functions=%lu\n",cases,failures,stats.functions);
 return failures?1:0;
}
