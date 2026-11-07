#include "common/cpu/recompiler/llvm/jit_service.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "field-fixtures.h"
#define MEMORY 0x20000
#define LO 0x1000
#define HI 0x6000
#define OBJ 0x9000
static u8 memory[MEMORY];
static GuestFn dispatch[(HI-LO)/4];
static SnxJitService* service;
static RtAPI rt;
static unsigned checks,failures;
static void check(int ok,const char* why){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",why);}}
static void put32(u64 p,u32 v){memcpy(memory+p,&v,4);}
static void put64(u64 p,u64 v){memcpy(memory+p,&v,8);}
static u32 get32(u64 p){u32 v;memcpy(&v,memory+p,4);return v;}
static u64 get64(u64 p){u64 v;memcpy(&v,memory+p,8);return v;}
static float getf(u64 p){float f;memcpy(&f,memory+p,4);return f;}
static void miss(CPU* c,u64 pc){fprintf(stderr,"unexpected external call %llx\n",(unsigned long long)pc);failures++;c->pc=0;}
static void undef(CPU* c,u64 pc,u32 w){fprintf(stderr,"undefined %08x at %llx\n",w,(unsigned long long)pc);failures++;c->pc=0;}
static void poll(CPU* c){c->poll=0;}
static void report(const SnxJitReport* r){if(r->error)fprintf(stderr,"JIT: %s\n",r->error);}
static CPU cpu(void){CPU c={0};c.mem=memory;c.rt=&rt;c.disp=dispatch;c.code_lo=LO;c.code_hi=HI;c.x[31]=0x1f000;return c;}
int main(void){
 memcpy(memory+0x1000,climb_start_code,sizeof climb_start_code);
 memcpy(memory+0x3000,dash_ramp_code,sizeof dash_ramp_code);
 memcpy(memory+0x4000,wormhole_tick_code,sizeof wormhole_tick_code);
 memcpy(memory+0x5000,respawn_tick_code,sizeof respawn_tick_code);
 SnxCodeImage images[4]={0};
 const u64 starts[]={0x1000,0x3000,0x4000,0x5000};
 const size_t lengths[]={sizeof climb_start_code,sizeof dash_ramp_code,sizeof wormhole_tick_code,sizeof respawn_tick_code};
 for(int i=0;i<4;i++){images[i].image=memory;images[i].image_size=MEMORY;images[i].text_off=starts[i];images[i].text_size=lengths[i];}
 SnxJitConfig config={0};config.images=images;config.image_count=4;config.dispatch=dispatch;config.code_lo=LO;config.code_hi=HI;config.threads=1;config.opt='2';config.report=report;
 rt.miss=miss;rt.undef=undef;rt.poll=poll;char error[512]={0};
 service=snx_jit_service_new(&config,error,sizeof error);if(!service){fprintf(stderr,"startup %s\n",error);return 2;}
 GuestFn climb=snx_jit_wait(service,0x1000),dash=snx_jit_wait(service,0x3448),wormhole=snx_jit_wait(service,0x4000),respawn=snx_jit_wait(service,0x5000);
 if(!climb||!dash||!wormhole||!respawn){snx_jit_service_free(service);return 2;}
 for(unsigned state=0;state<4;state++)for(unsigned timer=0;timer<=17;timer+=17){
    memset(memory+OBJ,0,0xe00);put32(OBJ+0xd00,state);put32(OBJ+0xd04,timer);
    put64(OBJ+0xd20,0x1122334455667788ull);put64(OBJ+0xd28,0x8877665544332211ull);
    put64(OBJ+0xd50,0x123456789abcdef0ull);put64(OBJ+0xd58,0xfedcba9876543210ull);
    CPU c=cpu();c.x[0]=OBJ;c.x[1]=1;climb(&c,0x1000);
    check(c.pc==0&&c.x[31]==0x1f000,"climbing stop initializer returns with balanced stack");
    check(get32(OBJ+0xd00)==(state==1?2:state)&&get32(OBJ+0xd04)==(state==1?0:timer),"climbing state1 to2 clears elapsed count through 64-bit store");
    check(get64(OBJ+0xd10)==(state==1?0x1122334455667788ull:0)&&get64(OBJ+0xd40)==(state==1?0x123456789abcdef0ull:0),"climbing initializer snapshots current vectors only for state1");
 }
 const int totals[]={0,1,2,30,120};
 for(int j=0;j<5;j++){
    int total=totals[j];const int counts[]={0,1,total-1,total,total+1,0x7fffffff};
    for(int k=0;k<6;k++){
        int before=counts[k],after=!total?0:before==0x7fffffff?0:before<total?before+1:total;
        put32(OBJ+0xd20,before);put32(OBJ+0xd00,0xdeadbeef);
        CPU c=cpu();c.x[19]=OBJ;c.x[20]=total;dash(&c,0x3448);
        check(c.pc==0,"dash ramp returns at extracted continuation");
        check((int)get32(OBJ+0xd20)==after,"dash ramp count increments, clamps or handles zero/overflow");
        check(getf(OBJ+0xd00)==(total?(float)after/total:0),"dash normalized progress matches count divided by configured duration");
    }
 }
 for(int before=-1;before<=31;before++){
    put32(OBJ,before);CPU c=cpu();c.x[20]=OBJ;wormhole(&c,0x4000);
    check((int)get32(OBJ)==before-1&&c.z==(before==1)&&c.n==(before<=0),"delayed effect entry decrements and exposes signed expiration flags");
 }
 const int holds[]={0,1,30,120};
 for(int i=0;i<4;i++){
    int n=holds[i],calls=0;put32(OBJ+0x2a0,0);
    while(calls<n+3){CPU c=cpu();c.x[19]=OBJ;c.x[20]=n;respawn(&c,0x5000);calls++;
        check(get32(OBJ+0x2a0)==(unsigned)calls,"respawn count advances before old-count comparison");
        int waiting=c.z||(c.n!=c.vf);if(!waiting)break;
    }
    check(calls==n+2,"respawn old-count <= threshold wait takes threshold+2 calls from zero");
    printf("respawn threshold=%d callbacks_until_old_count_exceeds_threshold=%d\n",n,calls);
 }
 SnxJitStats stats;snx_jit_stats(service,&stats);snx_jit_service_free(service);
 printf("field timer guest checks=%u failures=%u compiled_functions=%lu\n",checks,failures,stats.functions);return failures?1:0;
}
