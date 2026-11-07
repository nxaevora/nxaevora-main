#include "common/cpu/recompiler/llvm/jit_service.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dialogue-fixtures.h"
#define MEMORY 0x4200000
#define LO 0x7000
#define HI 0x2b33000
#define OBJ 0x4100000
#define INPUT 0x4101000
#define EXTERNAL 0x4102000
#define VT 0x4103000
#define PAYLOAD 0x4104000
#define OWNER 0x4105000
#define STACK 0x41f0000
static u8 memory[MEMORY];
static GuestFn dispatch[(HI-LO)/4];
static RtAPI rt;
static SnxJitService* service;
static unsigned checks, failures, steps, sounds, waits, busy, observed_delay, observed_quota;
static unsigned step_status[16],step_return[16];
static void check(int ok,const char* why){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",why);}}
static void put32(u64 p,u32 v){memcpy(memory+p,&v,4);}
static u32 get32(u64 p){u32 v;memcpy(&v,memory+p,4);return v;}
static void put64(u64 p,u64 v){memcpy(memory+p,&v,8);}
static void text_step(CPU* c,u64 pc){(void)pc;unsigned index=steps++;check(c->x[0]==OBJ,"text step object");check(index<16,"bounded fixture response sequence");observed_delay=memory[OBJ+0x9d];observed_quota=memory[OBJ+0xa4];put32(c->x[1],index<16?step_status[index]:1);c->x[0]=index<16?step_return[index]:4;c->pc=c->x[30];}
static void sound(CPU* c,u64 pc){(void)pc;sounds++;check(c->x[0]==OBJ+0x1d0,"sound event payload");c->pc=c->x[30];}
static void condition(CPU* c,u64 pc){(void)pc;waits++;check(c->x[0]==EXTERNAL,"external wait object");c->x[0]=busy;c->pc=c->x[30];}
static void miss(CPU* c,u64 pc){if(pc>=0x2b315e8&&pc<0x2b31fac){GuestFn f=snx_jit_wait(service,pc);if(f){dispatch[(pc-LO)/4]=f;f(c,pc);return;}}fprintf(stderr,"unexpected call %llx\n",(unsigned long long)pc);failures++;c->pc=0;}
static void undef(CPU* c,u64 pc,u32 w){fprintf(stderr,"undefined %x at %llx\n",w,(unsigned long long)pc);failures++;c->pc=0;}
static void poll(CPU* c){c->poll=0;}
static void report(const SnxJitReport* r){if(r->error)fprintf(stderr,"JIT %s\n",r->error);}
static void reset(void){memset(memory+OBJ,0,0x7000);put64(OBJ+0x118,INPUT);put64(EXTERNAL,VT);put64(VT+0x40,0x7020);put64(VT+0x50,0xfbfbd0);put64(OBJ,VT);put64(OBJ+0x1d8,PAYLOAD);memory[PAYLOAD]='s';steps=sounds=waits=busy=0;observed_delay=observed_quota=0;for(unsigned i=0;i<16;i++){step_status[i]=1;step_return[i]=4;}}
static u32 call_object(GuestFn f,u64 pc,u64 arg1,u64 object){CPU c={0};c.mem=memory;c.rt=&rt;c.disp=dispatch;c.code_lo=LO;c.code_hi=HI;c.x[0]=object;c.x[1]=arg1;c.x[31]=STACK;for(unsigned i=19;i<30;i++)c.x[i]=0x123400+i;f(&c,pc);int saved=1;for(unsigned i=19;i<30;i++)saved&=c.x[i]==0x123400+i;check(c.pc==0&&c.x[31]==STACK&&saved,"guest return, stack and saved-register preservation");return (u32)c.x[0];}
static u32 call(GuestFn f,u64 pc,u64 arg1){return call_object(f,pc,arg1,OBJ);}
int main(void){
 const u64 starts[]={0xfbfbd0,0xfc10e4,0xfc1078,0xfc1170,0x2b320d0,0x2b320c4,0x2b315e8,0x2737abc};
 const u8* codes[]={update_code,advance_code,advance_sound_code,tag_code,get_rate_code,set_rate_code,base_step_code,drain_code};
 const size_t sizes[]={sizeof update_code,sizeof advance_code,sizeof advance_sound_code,sizeof tag_code,sizeof get_rate_code,sizeof set_rate_code,sizeof base_step_code,sizeof drain_code};
 SnxCodeImage images[8]={0};for(unsigned i=0;i<8;i++){memcpy(memory+starts[i],codes[i],sizes[i]);images[i].image=memory;images[i].image_size=MEMORY;images[i].text_off=starts[i];images[i].text_size=sizes[i];}
 memcpy(memory+0x39847f8,switch_tables,sizeof switch_tables);
 dispatch[(0x7000-LO)/4]=text_step;dispatch[(0x7010-LO)/4]=sound;dispatch[(0x7020-LO)/4]=condition;
 rt.miss=miss;rt.undef=undef;rt.poll=poll;
 SnxJitConfig config={0};config.images=images;config.image_count=8;config.dispatch=dispatch;config.code_lo=LO;config.code_hi=HI;config.threads=1;config.opt='2';config.report=report;char err[512]={0};SnxJitService* s=snx_jit_service_new(&config,err,sizeof err);if(!s){fprintf(stderr,"startup %s\n",err);return 2;}
 service=s;GuestFn f[8];for(unsigned i=0;i<8;i++){f[i]=snx_jit_wait(s,starts[i]);if(!f[i])return 2;dispatch[(starts[i]-LO)/4]=f[i];}
 const unsigned ns[]={1,2,3,30,65535};
 for(unsigned mode=0;mode<2;mode++)for(unsigned j=0;j<5;j++){
  reset();put32(OBJ+0x188,ns[j]);memory[OBJ+0x19c]=mode;u32 r=call(f[0],starts[0],0);unsigned decrement=(ns[j]>1&&!mode)?2:1;
  check(get32(OBJ+0x188)==ns[j]-decrement,"tag wait uses original one/two-count decrement");check(r==1&&steps==0,"tag wait returns running without processing text");
 }
 for(unsigned mode=0;mode<2;mode++){
  reset();put32(OBJ+0x188,30);memory[OBJ+0x19c]=mode;unsigned count=0;while(!steps&&count<40){call(f[0],starts[0],0);count++;}check(count==(mode?31:16),"30-count tag resumes text on following update");
 }
 reset();put32(OBJ+0x188,30);put64(OBJ+0x190,EXTERNAL);memory[OBJ+0x198]=1;busy=1;call(f[0],starts[0],0);check(waits==1&&get32(OBJ+0x188)==30&&steps==0,"external wait freezes tag counter");busy=0;call(f[0],starts[0],0);check(memory[OBJ+0x198]==0&&get32(OBJ+0x188)==28,"external wait clears then resumes tag countdown");
 for(unsigned tag=0;tag<4;tag++){
  reset();unsigned tags[]={0xbe02,0xbe05,0xbe03,0xbd02};put32(PAYLOAD,tags[tag]);put32(PAYLOAD+8,30);u32 r=call(f[3],starts[3],PAYLOAD);
  check(get32(OBJ+0x188)==(tag==0?30:0),"only BE02 stores embedded wait count");check(r==(tag<2),"message tag handler acceptance");
 }
 for(unsigned mask=0;mask<16;mask++)for(unsigned disable=0;disable<2;disable++){
  reset();memory[INPUT+0xac]=mask;memory[OBJ+0x17b]=!disable;u32 r=call(f[1],starts[1],0);check(r==((mask&6)!=0&&!disable),"advance uses mask6 and input-enable gate");
 }
 for(unsigned method=1;method<=2;method++){
  reset();memory[OBJ+0x179]=1;check(call(f[method],starts[method],0)==1,"automatic-advance override");
  reset();memory[OBJ+0x17a]=1;check(call(f[method],starts[method],0)==1&&memory[OBJ+0x17a]==0,"one-shot advance consumed");check(call(f[method],starts[method],0)==0,"one-shot does not repeat");
  reset();memory[OBJ+0x178]=1;memory[OBJ+0x179]=1;memory[OBJ+0x17a]=1;check(call(f[method],starts[method],0)==0&&memory[OBJ+0x17a]==1,"advance lock dominates overrides");
 }
 reset();memory[INPUT+0xac]=2;memory[OBJ+0x17b]=1;check(call(f[2],starts[2],0)==1&&sounds==1,"accepted manual advance emits sound once");
 for(unsigned delay=0;delay<3;delay++)for(unsigned gate=0;gate<5;gate++){
  reset();memory[INPUT+0xa4]=2;memory[OBJ+0x17b]=1;memory[OBJ+0x17d]=1;memory[OBJ+0x9d]=delay;memory[OBJ+0xa4]=3;memory[OBJ+0xa5]=delay;memory[OBJ+0xa6]=3;
  if(gate==1)memory[OBJ+0x179]=1;if(gate==2)memory[OBJ+0x17b]=0;if(gate==3)memory[OBJ+0x17d]=0;if(gate==4)memory[OBJ+0x198]=1;
  call(f[0],starts[0],0);check(steps==1,"wrapper processes fixture step exactly once");check(observed_delay==(gate==0&&delay?delay-1:delay),"speed input decreases delay by one when enabled");check(observed_quota==(gate==0&&!delay?6:3),"zero-delay speed input doubles byte quota");check(memory[OBJ+0x9d]==delay&&memory[OBJ+0xa4]==3,"temporary speed setting restored from defaults");
 }
 /* Real base method: verified switch-table state4 enters byte-delay gate; all tested values stay on the non-parsing path. */
 reset();put32(OBJ+0xa8,4);memory[OBJ+0x9d]=3;
 for(unsigned i=1;i<=3;i++){check(call(f[6],starts[6],PAYLOAD)==1,"base reveal delay returns running");check(memory[OBJ+0x9e]==i&&get32(PAYLOAD)==0,"reveal byte count increases once per original invocation");}
 reset();put32(OBJ+0xa8,4);memory[OBJ+0x9d]=255;memory[OBJ+0x9e]=255;call(f[6],starts[6],PAYLOAD);check(memory[OBJ+0x9e]==0,"byte delay counter wraps at 255");
 for(unsigned mode=0;mode<2;mode++){
  reset();put64(OWNER+0xa8,OBJ);put32(OBJ+0x188,30);memory[OBJ+0x19c]=mode;
  call_object(f[7],starts[7],0,OWNER);check(get32(OBJ+0x188)==30&&steps==0,"drain disabled does not update text");
  memory[OWNER+0xd9]=1;call_object(f[7],starts[7],0,OWNER);
  check(get32(OBJ+0x188)==0&&steps==1&&get32(OWNER+0xb8)==4,"one enabled drain invocation consumes whole tag wait and records terminal status");
  check(observed_delay==0&&observed_quota==100,"drain overrides processing delay and quota");
 }
 reset();memory[OBJ+0xa4]=3;for(unsigned i=0;i<5;i++){step_status[i]=2;step_return[i]=0;}call(f[0],starts[0],0);check(steps==6,"nonzero out-status2 repeats beyond quota even when return is0");
 reset();memory[OBJ+0xa4]=3;for(unsigned i=0;i<8;i++){step_status[i]=0;step_return[i]=1;}call(f[0],starts[0],0);check(steps==3,"out-status0 with return1 consumes quota");
 reset();memory[OBJ+0xa4]=3;step_status[0]=0;step_return[0]=2;call(f[0],starts[0],0);check(steps==1,"out-status0 with return2 exits regardless of remaining quota");
 reset();memory[OBJ+0xa4]=3;step_status[0]=1;step_return[0]=1;call(f[0],starts[0],0);check(steps==1,"out-status1 stops even with running return and remaining quota");
 SnxJitStats stats;snx_jit_stats(s,&stats);snx_jit_service_free(s);printf("dialogue instruction checks=%u failures=%u compiled_functions=%lu\n",checks,failures,stats.functions);return failures?1:0;
}
