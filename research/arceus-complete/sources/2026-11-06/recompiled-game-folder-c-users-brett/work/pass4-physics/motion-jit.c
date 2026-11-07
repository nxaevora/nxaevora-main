#include "common/cpu/recompiler/llvm/jit_service.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "motion-fixture.h"
#define MEMORY 0x20000
#define LO 0x1000
#define HI 0x2000
#define WORLD 0x9000
#define MOTION 0xa000
#define STACK 0x18000
static u8 memory[MEMORY];
static GuestFn dispatch[(HI-LO)/4];
static RtAPI rt;
static unsigned checks,failures;
static void check(int ok,const char* why){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",why);}}
static void putf(u64 p,float f){memcpy(memory+p,&f,4);}
static float getf(u64 p){float f;memcpy(&f,memory+p,4);return f;}
static void putd(u64 p,double d){memcpy(memory+p,&d,8);}
static double getd(u64 p){double d;memcpy(&d,memory+p,8);return d;}
static void miss(CPU* c,u64 pc){fprintf(stderr,"unexpected external call %llx\n",(unsigned long long)pc);failures++;c->pc=0;}
static void undef(CPU* c,u64 pc,u32 w){fprintf(stderr,"undefined %08x at %llx\n",w,(unsigned long long)pc);failures++;c->pc=0;}
static void poll(CPU* c){c->poll=0;}
static void report(const SnxJitReport* r){if(r->error)fprintf(stderr,"JIT: %s\n",r->error);}
static CPU cpu(void){CPU c={0};c.mem=memory;c.rt=&rt;c.disp=dispatch;c.code_lo=LO;c.code_hi=HI;c.x[31]=STACK;c.x[24]=WORLD+0x530;c.x[28]=MOTION;return c;}
static void setup(float dt,const float* v,const double* p){
 putf(WORLD+0x540,dt);putf(WORLD+0x544,dt);
 for(int i=0;i<3;i++){putf(STACK+0x130+4*i,v[i]);putd(MOTION+0x10+8*i,p[i]);}
}
int main(void){
 memcpy(memory+LO,motion_code,sizeof motion_code);
 SnxCodeImage image={0};image.image=memory;image.image_size=MEMORY;image.text_off=LO;image.text_size=sizeof motion_code;
 SnxJitConfig config={0};config.images=&image;config.image_count=1;config.dispatch=dispatch;config.code_lo=LO;config.code_hi=HI;config.threads=1;config.opt='2';config.report=report;
 rt.miss=miss;rt.undef=undef;rt.poll=poll;char error[512]={0};
 SnxJitService* service=snx_jit_service_new(&config,error,sizeof error);
 if(!service){fprintf(stderr,"startup %s\n",error);return 2;}
 GuestFn move=snx_jit_wait(service,LO);if(!move){snx_jit_service_free(service);return 2;}
 const float times[]={0,1.0f/30,1.0f/60,1.0f/120,.007f,.1f,.25f};
 const float velocities[][3]={{3,0,-2},{0,2,-1},{-8,.25f,1.5f}};
 const double positions[][3]={{0,0,0},{1000,-25,100.5},{-200,50,1000000}};
 for(int t=0;t<7;t++)for(int v=0;v<3;v++)for(int p=0;p<3;p++){
    setup(times[t],velocities[v],positions[p]);CPU c=cpu();move(&c,LO);
    check(c.pc==0&&c.x[31]==STACK,"translation slice returns with stack unchanged");
    for(int i=0;i<3;i++){
        const double expected=positions[p][i]+(double)(float)(times[t]*velocities[v][i]);
        check(getd(MOTION+0x10+8*i)==expected,"motion double coordinate adds float(dt * input vector)");
        check(getf(STACK+0xc0+4*i)==(float)expected,"float coordinate cache matches updated double position");
    }
 }
 const unsigned rates[]={30,60,120};const float v[]={3,0,0};const double p[]={0,0,0};
 for(int kind=0;kind<2;kind++)for(int h=0;h<3;h++){
    unsigned hz=rates[h];float dt=kind?1.0f/hz:1.0f/30;setup(dt,v,p);
    for(unsigned i=0;i<hz;i++){CPU c=cpu();move(&c,LO);}
    double expected=(double)(float)(dt*3)*hz;
    check(getd(MOTION+0x10)==expected,"repeated actual translation arithmetic");
    printf("translation assumed_calls_per_second=%u dt_mode=%s dt=%.9g x_after_one_second=%.9f\n",hz,kind?"elapsed":"fixed_1_30",dt,getd(MOTION+0x10));
 }
 SnxJitStats stats;snx_jit_stats(service,&stats);snx_jit_service_free(service);
 printf("physics translation guest checks=%u failures=%u compiled_functions=%lu\n",checks,failures,stats.functions);
 return failures?1:0;
}
