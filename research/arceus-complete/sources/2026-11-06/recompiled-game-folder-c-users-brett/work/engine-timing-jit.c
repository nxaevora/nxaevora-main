#include "common/cpu/recompiler/llvm/jit_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine-timing-snippets.h"
#define MEMORY 0x20000
#define LO 0x1000
#define HI 0x9000
static u8 memory[MEMORY];
static GuestFn dispatch[(HI-LO)/4];
static SnxJitService* service;
static RtAPI rt;
static int failures,cases;
static void check(int ok,const char* label) {cases++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",label);}}
static void put32(u64 a,u32 v){memcpy(memory+a,&v,4);}
static u32 get32(u64 a){u32 v;memcpy(&v,memory+a,4);return v;}
static void put64(u64 a,u64 v){memcpy(memory+a,&v,8);}
static void putf(u64 a,float f){memcpy(memory+a,&f,4);}
static float getf(u64 a){float f;memcpy(&f,memory+a,4);return f;}
static void miss(CPU* c,u64 pc){GuestFn f=snx_jit_wait(service,pc);if(f)f(c,pc);else{failures++;c->pc=0;}}
static void undef(CPU* c,u64 pc,u32 w){fprintf(stderr,"undefined %08x at %llx\n",w,(unsigned long long)pc);failures++;c->pc=0;}
static void poll(CPU* c){c->poll=0;}
static void report(const SnxJitReport* r){if(r->error)fprintf(stderr,"JIT: %s\n",r->error);}
static CPU cpu(void){CPU c={0};c.mem=memory;c.rt=&rt;c.disp=dispatch;c.code_lo=LO;c.code_hi=HI;return c;}
static int near(float a,float b){return fabsf(a-b)<1e-6f*fmaxf(1.0f,fabsf(b));}
int main(void){
    memcpy(memory+0x1000,guest_select,sizeof guest_select);
    memcpy(memory+0x2000,guest_solver,sizeof guest_solver);
    memcpy(memory+0x3000,guest_context,sizeof guest_context);
    memcpy(memory+0x4000,guest_throw1,sizeof guest_throw1);
    memcpy(memory+0x5000,guest_throw2,sizeof guest_throw2);
    memcpy(memory+0x6000,guest_throw3,sizeof guest_throw3);
    memcpy(memory+0x7000,guest_recenter,sizeof guest_recenter);
    memcpy(memory+0x8000,guest_battle,sizeof guest_battle);
    for(unsigned i=0;i<3;i++) {
        put32(0x8800+i*0x40,0xaa0003e0|(19+i));put32(0x8804+i*0x40,0xd65f03c0);
        put32(0x8820+i*0x40,0x91000000|(19+i));put32(0x8824+i*0x40,0xd65f03c0);
    }
    SnxCodeImage image={0};image.image=memory;image.image_size=MEMORY;image.base=0;image.text_off=LO;image.text_size=HI-LO;
    SnxJitConfig config={0};config.images=&image;config.image_count=1;config.dispatch=dispatch;
    config.code_lo=LO;config.code_hi=HI;config.threads=1;config.opt='2';config.report=report;
    char error[512]={0};rt.miss=miss;rt.undef=undef;rt.poll=poll;
    service=snx_jit_service_new(&config,error,sizeof error);
    if(!service){fprintf(stderr,"JIT startup: %s\n",error);return 2;}
    GuestFn select=snx_jit_wait(service,0x1000),solver=snx_jit_wait(service,0x2000),context=snx_jit_wait(service,0x3000);
    if(!select||!solver||!context){snx_jit_service_free(service);return 2;}
    const float durations[]={0.0f,1.0f/30,1.0f/60,1.0f/120,0.007f,0.1f};
    const float rates[]={0,0.25f,1,2};
    for(int mode=0;mode<2;mode++)for(int d=0;d<6;d++)for(int r=0;r<4;r++){
        CPU c=cpu();c.x[20]=0xd000;c.v[8].f[0]=durations[d];memory[0xd121]=mode;putf(0xd124,rates[r]);
        select(&c,0x1000);
        float expected=(mode?durations[d]:1.0f/30)*rates[r];
        check(near(c.v[0].f[0],expected)&&c.pc==0,"physics selected duration");
        if(expected>0)for(int substeps=1;substeps<=4;substeps*=2){
            c=cpu();c.x[20]=0x9000;c.x[21]=0xa000;
            put32(0x95a0,substeps);put32(0x95a8,1);putf(0xa000,expected);putf(0xa004,0);
            putf(0x9870,1.0f);putf(0x9874,-9.81f);putf(0x9878,2.0f);putf(0x987c,0);
            solver(&c,0x2000);
            check(near(getf(0x9540),expected)&&near(getf(0x9548),1.0f/expected),"solver timestep and reciprocal");
            check(near(getf(0x9568),expected/substeps),"solver substep duration");
            check(near(getf(0x95d4),-9.81f*expected),"solver per-step vector scaling");
        }
    }
    put64(0xb000,0xb100);put64(0xa058,0xc000);
    // Exercise every reset/current-group/force-flag combination with real branches.
    for(int bits=0;bits<16;bits++){
        CPU c=cpu();c.x[0]=0xa000;c.x[20]=0xb000;c.x[21]=0;c.v[8].f[0]=1.0f/120;
        memory[0xb191]=!!(bits&1);memory[0xb190]=!!(bits&2);
        memory[0xa081]=!!(bits&4);memory[0xa082]=!!(bits&8);put32(0xa088,7);putf(0xa084,.25f);
        context(&c,0x3000);
        int reset=!!(bits&(1|4|8));
        check(get32(0xa088)==(reset?1:8),"script reset keeps integer tick");
        check(near(getf(0xa084),(reset?0:.25f)+1.0f/120),"script reset keeps elapsed seconds");
        check(memory[0xa080]==!!(bits&(2|4|8)),"script update eligibility flag");
    }
    const int fps_values[]={30,60,120};
    for(int f=0;f<3;f++)for(int group=1;group<=4;group*=2){
        int fps=fps_values[f];unsigned frame_sum=0,floor_sum=0;double seconds_sum=0;
        put32(0xa088,0);putf(0xa084,0);memory[0xa081]=memory[0xa082]=0;
        for(int i=0;i<fps;i++){
            CPU c=cpu();c.x[0]=0xa000;c.x[20]=0xb000;c.v[8].f[0]=1.0f/fps;
            memory[0xb190]=i%group==0;memory[0xb191]=i>0&&(i-1)%group==0;
            context(&c,0x3000);
            if(memory[0xa080]){frame_sum+=get32(0xa088);seconds_sum+=getf(0xa084);floor_sum+=(unsigned)floorf(getf(0xa084)*30.0f);}
        }
        check(fabs(frame_sum/seconds_sum-fps)<0.001,"batched script frame/seconds ratio");
        if(fps==120&&group==1)check(floor_sum==0,"naive floor fix stalls per-frame contexts");
        printf("script fps=%d group=%d consumed_frames=%u consumed_seconds=%.9f ratio=%.6f naive_floor_ticks=%u\n",fps,group,frame_sum,seconds_sum,frame_sum/seconds_sum,floor_sum);
    }
    const int authored_steps[]={120,60,90};
    for(int kind=0;kind<3;kind++){
        u64 pc=0x4000+kind*0x1000;GuestFn progress=snx_jit_wait(service,pc);
        if(!progress){failures++;continue;}
        for(int f=0;f<3;f++){
            int fps=fps_values[f],ticks=0;float expected_progress=0;putf(0xd328,0);
            while(getf(0xd328)<=1.0f && ticks<1000){
                CPU c=cpu();c.x[19]=c.x[20]=0xd000;progress(&c,pc);ticks++;
                expected_progress+=1.0f/authored_steps[kind];
                check(getf(0xd328)==expected_progress,"throwable authored progress increment with float rounding");
            }
            check(ticks>=authored_steps[kind]&&ticks<=authored_steps[kind]+1,"throwable unit-progress callback count");
            printf("throwable state=%d callbacks_until_above_one=%d assumed_hz=%d elapsed=%.6f\n",kind+1,ticks,fps,(double)ticks/fps);
        }
    }
    GuestFn recenter=snx_jit_wait(service,0x7000),battle=snx_jit_wait(service,0x8000);
    if(!recenter||!battle){failures++;}else{
        for(int total=0;total<=120;total+=30)for(int n=0;n<=total+2;n++){
            put32(0xd414,n);put32(0xd45c,total);CPU c=cpu();c.x[19]=0xd000;recenter(&c,0x7000);
            check(get32(0xd414)==n+1 && near(c.v[1].f[0],total>0?(float)(n+1)/total:1),"camera recenter tick and easing input");
        }
        for(int n=-2;n<=120;n++){
            put32(0xd238,n);CPU c=cpu();c.x[0]=0xd000;battle(&c,0x8000);
            check((int)get32(0xd238)==n-1 && c.z==(n==1) && c.n==(n<=0),"battle idle decrement and branch flags");
        }
    }
    // Execute the observational guard substitutions with every NZCV pattern.
    const u64 inputs[]={0,1,0xffffffff,0x100000000ull,0x8000000000000000ull,~0ull};
    for(unsigned i=0;i<3;i++){
        u64 pc=0x8800+i*0x40;GuestFn original=snx_jit_wait(service,pc),patched=snx_jit_wait(service,pc+0x20);
        if(!original||!patched){failures++;continue;}
        for(int n=0;n<6;n++)for(int flags=0;flags<16;flags++){
            CPU a=cpu();a.x[0]=inputs[n];set_nzcv(&a,flags);CPU b=a;
            original(&a,pc);patched(&b,pc+0x20);
            check(!memcmp(a.x,b.x,sizeof a.x)&&get_nzcv(&a)==get_nzcv(&b)&&a.pc==b.pc,"MOV to ADD observational guard equivalence");
        }
    }
    SnxJitStats stats;snx_jit_stats(service,&stats);snx_jit_service_free(service);
    printf("isolated engine instruction checks=%d failures=%d compiled_functions=%lu\n",cases,failures,stats.functions);
    return failures?1:0;
}
