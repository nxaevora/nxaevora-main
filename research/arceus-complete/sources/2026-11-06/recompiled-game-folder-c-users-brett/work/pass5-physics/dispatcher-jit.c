#include "common/cpu/recompiler/llvm/jit_service.h"
#include <stdio.h>
#include <string.h>
#include "dispatcher-fixture.h"
#define LO 0x1000
#define HI 0x9000
#define MEMORY 0x20000
#define OBJECT 0x10000
#define CONTEXT 0x11000
#define CLOCK 0x12000
#define CLOCK_VTABLE 0x12100
#define RECORD 0x12200
#define OBJECT_VTABLE 0x12300
#define STACK 0x1f000
#define CLOCK_STUB 0x8000
#define CALLBACK_STUB 0x8100
static u8 memory[MEMORY];static GuestFn dispatch[(HI-LO)/4];static RtAPI rt;
static unsigned checks,failures,clock_calls,listeners,enqueues,callbacks;
static u64 delta;
static void check(int ok,const char* why){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",why);}}
static void put64(u64 p,u64 v){memcpy(memory+p,&v,8);}static void put32(u64 p,u32 v){memcpy(memory+p,&v,4);}
static u32 get32(u64 p){u32 v;memcpy(&v,memory+p,4);return v;}
static void clock_stub(CPU*c,u64 pc){(void)pc;check(c->x[0]==CLOCK&&c->x[1]==1,"clock selector and object");clock_calls++;put64(RECORD+8,delta);c->x[0]=RECORD;c->pc=c->x[30];}
static void listener_stub(CPU*c,u64 pc){(void)pc;check(c->x[1]==CONTEXT,"listener context");listeners++;c->pc=c->x[30];}
static void enqueue_stub(CPU*c,u64 pc){(void)pc;check(c->x[1]==OBJECT&&c->x[2]==CONTEXT,"enqueue object/context");enqueues++;c->pc=c->x[30];}
static void callback_stub(CPU*c,u64 pc){(void)pc;check(c->x[0]==OBJECT&&c->x[1]==CONTEXT,"callback object/context");callbacks++;c->pc=c->x[30];}
static void miss(CPU*c,u64 pc){fprintf(stderr,"unexpected external %llx\n",(unsigned long long)pc);failures++;c->pc=0;}
static void undef(CPU*c,u64 pc,u32 w){fprintf(stderr,"undefined %08x at %llx\n",w,(unsigned long long)pc);failures++;c->pc=0;}
static void poll(CPU*c){c->poll=0;}
static void report(const SnxJitReport*r){if(r->error)fprintf(stderr,"JIT: %s\n",r->error);}
static void setup(void){memset(memory+OBJECT,0,MEMORY-OBJECT);clock_calls=listeners=enqueues=callbacks=0;put64(CONTEXT,CLOCK);put32(CONTEXT+8,0x12345678);put32(CONTEXT+12,0);put64(CLOCK,CLOCK_VTABLE);put64(CLOCK_VTABLE,CLOCK_STUB);put64(OBJECT,OBJECT_VTABLE);put64(OBJECT_VTABLE+0x70,CALLBACK_STUB);put32(OBJECT+0x398,1);put32(OBJECT+0x37c,0xaabbccdd);}
static CPU cpu(void){CPU c={0};c.mem=memory;c.rt=&rt;c.disp=dispatch;c.code_lo=LO;c.code_hi=HI;c.x[31]=STACK;c.x[0]=OBJECT;c.x[1]=CONTEXT;return c;}
int main(void){
 memcpy(memory+READY,ready_code,sizeof ready_code);memcpy(memory+INVOKE,invoke_code,sizeof invoke_code);
 dispatch[(CLOCK_STUB-LO)/4]=clock_stub;dispatch[(CALLBACK_STUB-LO)/4]=callback_stub;
 dispatch[(READY_LISTENER-LO)/4]=listener_stub;dispatch[(INVOKE_LISTENER-LO)/4]=listener_stub;dispatch[(ENQUEUE-LO)/4]=enqueue_stub;
 SnxCodeImage images[2]={{0},{0}};images[0].image=memory;images[0].image_size=MEMORY;images[0].text_off=READY;images[0].text_size=sizeof ready_code;
 images[1].image=memory;images[1].image_size=MEMORY;images[1].text_off=INVOKE;images[1].text_size=sizeof invoke_code;
 SnxJitConfig config={0};config.images=images;config.image_count=2;config.dispatch=dispatch;config.code_lo=LO;config.code_hi=HI;config.threads=1;config.opt='2';config.report=report;
 rt.miss=miss;rt.undef=undef;rt.poll=poll;char error[512]={0};SnxJitService*s=snx_jit_service_new(&config,error,sizeof error);if(!s){fprintf(stderr,"startup %s\n",error);return 2;}
 GuestFn ready=snx_jit_wait(s,READY),invoke=snx_jit_wait(s,INVOKE);if(!ready||!invoke)return 2;
 unsigned scenarios=0;
 for(unsigned active=0;active<2;active++)for(unsigned blocked=0;blocked<2;blocked++)for(unsigned nonzero=0;nonzero<2;nonzero++)for(unsigned deps=0;deps<4;deps++)for(unsigned initial=0;initial<4;initial++){
  setup();scenarios++;memory[OBJECT+0x1e0]=active;put32(OBJECT+0x1e8,blocked);delta=nonzero?8333333:0;
  put64(OBJECT+0x70,0x14000);put64(OBJECT+0x78,0x14000+deps*8);put32(OBJECT+0x1f0,initial);
  CPU c=cpu();ready(&c,READY);int should_clock=active&&!blocked;int execute=should_clock&&nonzero;int last=deps==0||initial+1==deps;
  check(c.pc==0&&c.x[31]==STACK,"readiness return/stack");check(clock_calls==should_clock,"clock gate");check(enqueues==(unsigned)(execute&&last),"enqueue gate");check(listeners==(unsigned)((!execute&&last)?2:0),"bypass listeners");
  check(get32(OBJECT+0x37c)==(u32)((!execute&&last)?0x12345678:0xaabbccdd),"completion epoch only on completed bypass");
  check(get32(OBJECT+0x1f0)==(u32)((!execute&&last)?0:(deps?initial+1:initial)),"fan-in counter");
 }
 for(unsigned skip=0;skip<2;skip++){setup();scenarios++;memory[OBJECT+0x39c]=skip;CPU c=cpu();invoke(&c,INVOKE);check(c.pc==0&&c.x[31]==STACK,"dispatch return/stack");check(callbacks==1-skip,"no-owner skip flag");check(listeners==1-skip,"pre-callback listener");check(clock_calls==0,"final dispatch no clock access");}
 /* Same epoch twice: readiness has no last-epoch equality rejection. */
 setup();memory[OBJECT+0x1e0]=1;delta=0;for(unsigned i=0;i<2;i++){CPU c=cpu();ready(&c,READY);}check(listeners==4,"same epoch does not suppress repeated bypass");
 setup();memory[OBJECT+0x1e0]=1;delta=8333333;for(unsigned i=0;i<2;i++){CPU c=cpu();ready(&c,READY);}check(enqueues==2,"same epoch does not suppress repeated readiness");
 SnxJitStats stats;snx_jit_stats(s,&stats);snx_jit_service_free(s);printf("dispatcher guest scenarios=%u checks=%u failures=%u compiled_functions=%lu\n",scenarios,checks,failures,stats.functions);return failures?1:0;
}
