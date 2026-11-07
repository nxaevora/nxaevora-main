#include "common/cpu/recompiler/llvm/jit_service.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "scheduler-fixtures.h"
#define MEMORY 0x20000
#define LO 0x1000
#define HI 0x4000
#define CATEGORY 0xa000
#define TABLE 0xb000
#define BUCKET(i) (0xc000+0x100*(i))
#define CONTEXT(i) (0x10000+0x100*(i))
static u8 memory[MEMORY];static GuestFn dispatch[(HI-LO)/4];static SnxJitService* service;static RtAPI rt;static unsigned tests,failures;
static void check(int ok,const char*s){tests++;if(!ok){failures++;if(failures<20)fprintf(stderr,"FAIL %s\n",s);}}
static void put32(u64 a,u32 v){memcpy(memory+a,&v,4);}static u32 get32(u64 a){u32 v;memcpy(&v,memory+a,4);return v;}
static void put64(u64 a,u64 v){memcpy(memory+a,&v,8);}static void putf(u64 a,float v){memcpy(memory+a,&v,4);}static float getf(u64 a){float v;memcpy(&v,memory+a,4);return v;}
static void miss(CPU*c,u64 pc){fprintf(stderr,"Unexpected miss %llx\n",(unsigned long long)pc);failures++;c->pc=0;}
static void undef(CPU*c,u64 pc,u32 w){fprintf(stderr,"Undefined %x at %llx\n",w,(unsigned long long)pc);failures++;c->pc=0;}
static void poll(CPU*c){c->poll=0;}static void report(const SnxJitReport*r){if(r->error)fprintf(stderr,"JIT %s\n",r->error);}
static CPU cpu(void){CPU c={0};c.mem=memory;c.rt=&rt;c.disp=dispatch;c.code_lo=LO;c.code_hi=HI;return c;}
static int near(float a,float b){return fabsf(a-b)<1e-6f*fmaxf(1,fabsf(b));}
int main(void){
 const u64 pc[]={0x1000,0x2000,0x3000};const u32* bodies[]={guest_round,guest_bucket,guest_context};const size_t lengths[]={sizeof guest_round,sizeof guest_bucket,sizeof guest_context};SnxCodeImage images[3]={0};
 for(int i=0;i<3;i++){memcpy(memory+pc[i],bodies[i],lengths[i]);images[i].image=memory;images[i].image_size=MEMORY;images[i].text_off=pc[i];images[i].text_size=lengths[i];}
 SnxJitConfig cfg={0};cfg.images=images;cfg.image_count=3;cfg.dispatch=dispatch;cfg.code_lo=LO;cfg.code_hi=HI;cfg.threads=1;cfg.opt='2';cfg.report=report;rt.miss=miss;rt.undef=undef;rt.poll=poll;char error[512]={0};
 service=snx_jit_service_new(&cfg,error,sizeof error);if(!service){fprintf(stderr,"startup %s\n",error);return 2;}
 GuestFn round=snx_jit_wait(service,pc[0]),bucket=snx_jit_wait(service,pc[1]),context=snx_jit_wait(service,pc[2]);if(!round||!bucket||!context)return 2;
 const int counts[]={1,4,6,8,16},fps_values[]={30,60,120};
 for(int k=0;k<5;k++)for(int h=0;h<3;h++)for(int force_mode=0;force_mode<3;force_mode++){
  int n=counts[k],fps=fps_values[h],eligible[16]={0},old_current[16],expected_count[16]={0};float expected_seconds[16]={0};unsigned selected=0;
  put32(CATEGORY+0xc8,0);put32(CATEGORY+0xcc,n);
  for(int j=0;j<n;j++){put64(TABLE+8*j,BUCKET(j));put64(CONTEXT(j)+0x58,0xe000);memory[BUCKET(j)+0x90]=1;memory[BUCKET(j)+0x91]=0;old_current[j]=1;put32(CONTEXT(j)+0x88,0);putf(CONTEXT(j)+0x84,0);}
  for(int frame=0;frame<n*8;frame++){
   CPU c=cpu();c.x[22]=CATEGORY;round(&c,pc[0]);check(c.x[15]==(unsigned)(frame%n)&&get32(CATEGORY+0xc8)==(unsigned)((frame+1)%n),"round uses old index and advances modulo bucket count");
   for(int j=0;j<n;j++){
    int current=j==frame%n,force81=force_mode==1&&j==0&&frame>=n&&frame<3*n,force82=force_mode==2&&j==n-1;
    c.x[20]=TABLE;c.x[21]=j;bucket(&c,pc[1]);check(memory[BUCKET(j)+0x90]==current&&memory[BUCKET(j)+0x91]==old_current[j],"bucket current/previous flags");
    int reset=old_current[j]||force81||force82;if(reset){expected_count[j]=0;expected_seconds[j]=0;}expected_count[j]++;expected_seconds[j]+=1.0f/fps;
    memory[CONTEXT(j)+0x81]=force81;memory[CONTEXT(j)+0x82]=force82;c.x[0]=CONTEXT(j);c.v[8].f[0]=1.0f/fps;context(&c,pc[2]);
    check(get32(CONTEXT(j)+0x88)==(unsigned)expected_count[j]&&near(getf(CONTEXT(j)+0x84),expected_seconds[j]),"context reset/elapsed and count accumulation");
    check(memory[CONTEXT(j)+0x80]==(current||force81||force82),"context eligibility independent of elapsed nanoseconds");
    if(current){selected++;eligible[j]++;}old_current[j]=current;
   }
  }
  check(selected==(unsigned)n*8,"exactly one round-robin bucket selected per callback");for(int j=0;j<n;j++)check(eligible[j]==8,"each bucket selected once per cycle");
  if(!force_mode)printf("groups=%d assumed_scheduler_hz=%d per_bucket_selection_hz=%.6f cycles=8\n",n,fps,(double)fps/n);
 }
 // Zero elapsed seconds still rotates groups and adds integer frame count.
 put32(CATEGORY+0xc8,0);put32(CATEGORY+0xcc,4);put64(TABLE,BUCKET(0));memory[BUCKET(0)+0x90]=0;memory[BUCKET(0)+0x91]=0;memory[CONTEXT(0)+0x81]=memory[CONTEXT(0)+0x82]=0;put32(CONTEXT(0)+0x88,5);putf(CONTEXT(0)+0x84,.25f);
 CPU c=cpu();c.x[22]=CATEGORY;round(&c,pc[0]);c.x[20]=TABLE;bucket(&c,pc[1]);c.x[0]=CONTEXT(0);c.v[8].f[0]=0;context(&c,pc[2]);check(get32(CATEGORY+0xc8)==1&&get32(CONTEXT(0)+0x88)==6&&getf(CONTEXT(0)+0x84)==.25f,"zero dt does not pause scheduler internally");
 SnxJitStats stats;snx_jit_stats(service,&stats);snx_jit_service_free(service);printf("Scheduler guest checks=%u failures=%u compiled_functions=%lu\n",tests,failures,stats.functions);return failures?1:0;
}
