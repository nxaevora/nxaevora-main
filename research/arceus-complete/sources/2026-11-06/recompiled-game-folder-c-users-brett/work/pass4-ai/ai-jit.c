#include "common/cpu/recompiler/llvm/jit_service.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ai-fixtures.h"
#define MEMSIZE 0x4300000
#define LO 0xb32e00
#define HI 0x1059a80
#define OBJ 0x4200000
#define CONTEXT 0x4201000
#define OUT 0x4202000
#define STACK 0x42ff000
static u8 memory[MEMSIZE];
static GuestFn dispatch[(HI-LO)/4];
static SnxJitService* service;static RtAPI rt;static unsigned tests,failures,getter_calls,factory_calls;
static void check(int ok,const char* s){tests++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",s);}}
static void put32(u64 a,u32 x){memcpy(memory+a,&x,4);}static u32 get32(u64 a){u32 x;memcpy(&x,memory+a,4);return x;}
static void put64(u64 a,u64 x){memcpy(memory+a,&x,8);}
static void miss(CPU* c,u64 pc){
 if(pc==0xa5f8a8){factory_calls++;put64(c->x[8],CONTEXT);c->pc=c->x[30];return;}
 if(pc==0x2943a5c){c->x[0]=0;c->pc=c->x[30];return;}
 if(pc==0x1059a78)getter_calls++;
 GuestFn f=snx_jit_wait(service,pc);if(f){f(c,pc);return;}
 fprintf(stderr,"Unexpected miss %llx\n",(unsigned long long)pc);failures++;c->pc=0;
}
static void undef(CPU*c,u64 pc,u32 w){fprintf(stderr,"undefined %x at %llx\n",w,(unsigned long long)pc);failures++;c->pc=0;}
static void poll(CPU*c){c->poll=0;}static void report(const SnxJitReport*r){if(r->error)fprintf(stderr,"JIT %s\n",r->error);}
static CPU cpu(void){CPU c={0};c.mem=memory;c.rt=&rt;c.disp=dispatch;c.code_lo=LO;c.code_hi=HI;c.x[31]=STACK;return c;}
static void mulq(const float*a,const float*b,float*r){r[0]=a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1];r[1]=a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0];r[2]=a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3];r[3]=a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2];}
static void forward(const float*q,float*r){float z[4]={0,0,1,0},conj[4]={-q[0],-q[1],-q[2],q[3]},t[4];mulq(q,z,t);mulq(t,conj,r);}
static int near4(const float*a,const float*b){for(int i=0;i<4;i++)if(fabsf(a[i]-b[i])>1e-5f)return 0;return 1;}
int main(void){
 const u64 addresses[]={0xb35ae0,0xbd4380,0xbd43a4,0x1059a78,0xb32e34,0xb33ad4,0xb32e00,0xb33aa0};
 const size_t lengths[]={sizeof wait_code,sizeof success_code,sizeof running_code,sizeof getter_code,sizeof turn_code,sizeof turn2_code,sizeof count1_code,sizeof count2_code};
 const u8* bodies[]={wait_code,success_code,running_code,getter_code,turn_code,turn2_code,count1_code,count2_code};SnxCodeImage images[8]={0};
 for(int i=0;i<8;i++){memcpy(memory+addresses[i],bodies[i],lengths[i]);images[i].image=memory;images[i].image_size=MEMSIZE;images[i].text_off=addresses[i];images[i].text_size=lengths[i];}
 memcpy(memory+0x3788000,turn_constants,sizeof turn_constants);put32(0x40e7bf8,1);put32(0x40e7c04,4);
 SnxJitConfig config={0};config.images=images;config.image_count=8;config.dispatch=dispatch;config.code_lo=LO;config.code_hi=HI;config.threads=1;config.opt='2';config.report=report;
 rt.miss=miss;rt.undef=undef;rt.poll=poll;char error[512]={0};service=snx_jit_service_new(&config,error,sizeof error);if(!service){fprintf(stderr,"startup %s\n",error);return 2;}
 GuestFn wait=snx_jit_wait(service,0xb35ae0),turn=snx_jit_wait(service,0xb32e34);if(!wait||!turn)return 2;
 const int durations[]={-1,0,1,2,10,30};const unsigned frames[]={0,1,2,4,30,120};
 for(int i=0;i<6;i++)for(int j=0;j<6;j++){
  int before=durations[i];put32(OBJ+0x13c,before);put32(CONTEXT+0x88,frames[j]);put32(OUT,99);CPU c=cpu();c.x[0]=OBJ;c.x[8]=OUT;
  unsigned factories=factory_calls;wait(&c,0xb35ae0);
  int after=before<1?before:before-(int)frames[j];
  check((int)get32(OBJ+0x13c)==after,"WaitFrame subtracts only while positive");
  check(get32(OUT)==(before<1?1:4),"WaitFrame result uses pre-decrement remaining count");
  check(get32(CONTEXT+0x88)==frames[j],"WaitFrame getter is non-consuming");
  check(factory_calls-factories==(unsigned)(before>0),"WaitFrame does not read context once completed");
  check(c.pc==0&&c.x[31]==STACK,"WaitFrame returns with balanced stack");
 }
 for(int hz=30;hz<=120;hz*=2){
  put32(OBJ+0x13c,30);put32(CONTEXT+0x88,1);unsigned calls=0;
  do{CPU c=cpu();c.x[0]=OBJ;c.x[8]=OUT;wait(&c,0xb35ae0);calls++;}while(get32(OUT)==4&&calls<40);
  check(calls==31,"Thirty authored ticks need 31 WaitFrame callbacks including completion");
  printf("WaitFrame duration=30 frame_delta=1 calls_to_success=%u assumed_hz=%d elapsed=%.9f\n",calls,hz,(double)calls/hz);
 }
 const float angles[]={-.6f,-.2f,0,.3f,.8f};
 unsigned noncommuting=0;
 for(int variant=0;variant<2;variant++){
  u64 turnpc=variant?0xb33ad4:0xb32e34,countpc=variant?0xb33aa0:0xb32e00;
  u32 qfield=variant?0x140:0x150,countfield=variant?0x134:0x144;
  GuestFn turn=snx_jit_wait(service,turnpc),counter=snx_jit_wait(service,countpc);if(!turn||!counter)return 2;
  for(int i=0;i<5;i++)for(int j=0;j<5;j++){
   float current[4]={sinf(angles[i]/2),0,0,cosf(angles[i]/2)},step[4]={0,sinf(angles[j]/2),0,cosf(angles[j]/2)};
   float postq[4],preq[4],post[4],pre[4],out[4];mulq(current,step,postq);mulq(step,current,preq);forward(postq,post);forward(preq,pre);
   for(int delta=0;delta<=4;delta+=delta?3:1){
    memcpy(memory+STACK,current,16);memcpy(memory+OBJ+qfield,step,16);put32(OBJ+countfield,7);
    CPU c=cpu();c.x[20]=OBJ;c.x[21]=OBJ;c.x[0]=delta;counter(&c,countpc);turn(&c,turnpc);memcpy(out,memory+STACK+0x10,16);
    check(get32(OBJ+countfield)==7+delta,"Turn completion counter advances by getter result");
    check(near4(out,pre),"Turn applies step * current quaternion to forward vector, even when frame delta is zero");
    check(c.pc==0&&c.x[31]==STACK,"Turn slices return with unchanged stack");
   }
   if(i!=2&&j!=2){check(!near4(out,post),"Noncommuting cases distinguish pre- from post-multiplication");noncommuting++;}
  }
 }
 printf("Turn/TurnKey: 50 quaternion pairs tested at frame deltas 0,1,4; %u noncommuting pairs distinguish step * current order.\n",noncommuting);
 SnxJitStats stats;snx_jit_stats(service,&stats);snx_jit_service_free(service);printf("AI guest checks=%u failures=%u compiled_functions=%lu\n",tests,failures,stats.functions);return failures?1:0;
}
