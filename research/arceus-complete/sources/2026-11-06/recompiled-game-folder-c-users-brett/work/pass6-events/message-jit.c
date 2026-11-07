#include "common/cpu/recompiler/llvm/jit_service.h"
#include <stdio.h>
#include <string.h>
#include "message-fixtures.h"
#define MEMORY 0x4200000
#define LO 0x7000
#define HI 0x2b33000
#define OBJ 0x4100000
#define INPUT 0x4101000
#define VT 0x4103000
#define EMPTY 0x4104000
#define OWNER 0x4105000
#define OVT 0x4106000
#define WRAP 0x4107000
#define STACK 0x41f0000
static u8 memory[MEMORY];static GuestFn dispatch[(HI-LO)/4];static SnxJitService*service;static RtAPI rt;
static unsigned checks,failures,animation_done,animation_calls,close_calls,color_calls,layout_calls,timed_close_calls;
static void check(int ok,const char*s){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",s);}}
static void put32(u64 p,u32 v){memcpy(memory+p,&v,4);}static u32 get32(u64 p){u32 v;memcpy(&v,memory+p,4);return v;}
static void put64(u64 p,u64 v){memcpy(memory+p,&v,8);}
static void miss(CPU*c,u64 pc){
 switch(pc){
 case 0x7000:color_calls++;c->x[0]=0;break;
 case 0x7010:layout_calls++;break;
 case 0x7020:animation_calls++;c->x[0]=animation_done;break;
 case 0x7030:close_calls++;break;
 case 0x7040:break;
 case 0x2737c00:timed_close_calls++;check(c->x[0]==OWNER&&c->x[1]==1,"timed subtype requests animated close on its message");put32(OWNER+0x80,5);break;
 default:if(pc>=LO&&pc<HI){GuestFn f=snx_jit_wait(service,pc);if(f){f(c,pc);return;}}fprintf(stderr,"unexpected call %llx\n",(unsigned long long)pc);failures++;c->pc=0;return;
 }c->pc=c->x[30];
}
static void undef(CPU*c,u64 pc,u32 w){fprintf(stderr,"undefined %x at%llx\n",w,(unsigned long long)pc);failures++;c->pc=0;}
static void poll(CPU*c){c->poll=0;}static void report(const SnxJitReport*r){if(r->error)fprintf(stderr,"JIT %s\n",r->error);}
static void reset(void){
 memset(memory+OBJ,0,0x8000);put64(OBJ,VT);put64(OBJ+0x118,INPUT);put64(OBJ+0x1d8,EMPTY);put64(WRAP+0x68,OWNER);
 put64(VT+0x50,0xfbfbd0);put64(VT+0x68,0xfc1078);put64(VT+0x70,0xfc10e4);put64(VT+0x78,0x7000);put64(VT+0x80,0x7010);
 put64(OWNER,OVT);put64(OWNER+0xa8,OBJ);put64(OVT+0x88,0x2737d94);put64(OVT+0x70,0x7020);put64(OVT+0x98,0x7030);put64(OVT+0xb0,0x7020);put64(OVT+0xc0,0x7040);
 memory[OBJ+0x17b]=1;memory[OBJ+0xa4]=memory[OBJ+0xa6]=1;animation_done=animation_calls=close_calls=color_calls=layout_calls=timed_close_calls=0;
}
static u32 call(GuestFn f,u64 pc,u64 object){CPU c={0};c.mem=memory;c.rt=&rt;c.disp=dispatch;c.code_lo=LO;c.code_hi=HI;c.x[0]=object;c.x[31]=STACK;for(unsigned i=19;i<30;i++)c.x[i]=0x123400+i;f(&c,pc);int saved=1;for(unsigned i=19;i<30;i++)saved&=c.x[i]==0x123400+i;check(c.pc==0&&c.x[31]==STACK&&saved,"guest return and saved registers");return c.x[0];}
int main(void){
 const u64 starts[]={0xfbfbd0,0xfc1078,0xfc10e4,0x2b320c4,0x2b320d0,0x2b315e8,0x2b31fac,0x27379b8,0x2737abc,0x2737d94,0x2485390,0x2484ecc};
 const u8*codes[]={wrapper_code,advance_sound_code,advance_code,rate_set_code,rate_get_code,base_code,colors_code,parent_code,drain_code,state_code,timed_close_code,close_forward_code};
 const size_t sizes[]={sizeof wrapper_code,sizeof advance_sound_code,sizeof advance_code,sizeof rate_set_code,sizeof rate_get_code,sizeof base_code,sizeof colors_code,sizeof parent_code,sizeof drain_code,sizeof state_code,sizeof timed_close_code,sizeof close_forward_code};
 SnxCodeImage images[12]={0};for(int i=0;i<12;i++){memcpy(memory+starts[i],codes[i],sizes[i]);images[i].image=memory;images[i].image_size=MEMORY;images[i].text_off=starts[i];images[i].text_size=sizes[i];}
 memcpy(memory+0x39847f8,base_tables_code,sizeof base_tables_code);memcpy(memory+0x397e422,owner_tables_code,sizeof owner_tables_code);
 rt.miss=miss;rt.undef=undef;rt.poll=poll;SnxJitConfig cfg={0};cfg.images=images;cfg.image_count=12;cfg.dispatch=dispatch;cfg.code_lo=LO;cfg.code_hi=HI;cfg.threads=1;cfg.opt='2';cfg.report=report;char error[512]={0};service=snx_jit_service_new(&cfg,error,sizeof error);if(!service){fprintf(stderr,"startup%s\n",error);return 2;}
 GuestFn f[12];for(int i=0;i<12;i++){f[i]=snx_jit_wait(service,starts[i]);if(!f[i])return 2;}
 for(unsigned state=0;state<9;state++)for(unsigned status=0;status<6;status++)for(unsigned mode=0;mode<2;mode++)for(unsigned done=0;done<2;done++){
  reset();put32(OWNER+0x80,state);put32(OWNER+0xb8,status);put32(OWNER+0xe0,mode?4:0);animation_done=done;memory[OWNER+0xd8]=1;
  call(f[9],starts[9],OWNER);unsigned expected=state;
  if(state==1||state==4){if(status==1)expected=4;else if(status==2)expected=1;else if((status==0||status==4)&&status==(mode?4:0))expected=6;}
  if(state==2)expected=done?4:3;if(state==3&&done)expected=4;if(state==5&&done)expected=7;
  check(get32(OWNER+0x80)==expected,"owner status/state dispatch");check(close_calls==(state==2),"opening transition callback only in entry state2");check(animation_calls==(state==2||state==3||state==5),"animation completion gate selected by entry state");check(memory[OWNER+0xd8]==!(state==5&&done),"D8 clears only on completed close transition");
 }
 // Full wrapper changes the readiness latch only on completion of its inner loop.
 reset();put32(OBJ+0xa8,0);put32(OBJ+0x90,1);put32(OBJ+0x114,1);memory[INPUT+0xac]=2;
 check(call(f[0],starts[0],OBJ)==0&&get32(OBJ+0xa8)==0&&get32(OBJ+0x114)==2,"unarmed manual wait rejects then arms at wrapper end");
 check(call(f[0],starts[0],OBJ)==4&&get32(OBJ+0xa8)==1,"next wrapper accepts same still-present fresh mask");check(memory[INPUT+0xac]==2,"manual predicate does not consume shared fresh mask");
 reset();put32(OBJ+0xa8,0);put32(OBJ+0x90,1);put32(OBJ+0x114,1);put32(OBJ+0x188,1);call(f[0],starts[0],OBJ);check(get32(OBJ+0x114)==1,"embedded-wait early return does not arm latch");
 const unsigned thresholds[]={0,1,2,30,254,255};for(unsigned k=0;k<6;k++){
  reset();put32(OBJ+0xa8,0);put32(OBJ+0x90,2);memory[OBJ+0xa2]=thresholds[k];unsigned advanced=0;
  for(unsigned n=1;n<=257;n++){u32 result=call(f[0],starts[0],OBJ);if(result==4){advanced=n;break;}}
  check(advanced==(thresholds[k]<255?thresholds[k]+1:0),"automatic byte delay strict threshold and 255 wrap");printf("automatic threshold=%u advance_wrapper=%u\n",thresholds[k],advanced);
 }
 reset();put32(OBJ+0xa8,0);put32(OBJ+0x90,3);put32(OBJ+0x114,2);memory[OBJ+0xa2]=30;memory[INPUT+0xac]=2;
 check(call(f[0],starts[0],OBJ)==4&&memory[OBJ+0xa3]==0,"mode3 manual acceptance bypasses automatic delay");
 // Synthetic terminal page-wait path: original core returns status0, then next call can acknowledge end.
 for(unsigned drain=0;drain<2;drain++)for(unsigned one_shot=0;one_shot<2;one_shot++)for(unsigned cursor=0;cursor<2;cursor++){
  reset();put32(OWNER+0x80,1);put32(OWNER+0xe0,4);memory[OWNER+0xd9]=drain;memory[OWNER+0xd8]=cursor;put32(OBJ+0xa8,2);put32(OBJ+0x90,1);put32(OBJ+0x114,2);memory[OBJ+0x99]=memory[OBJ+0x9b]=1;memory[OBJ+0x17a]=one_shot;memory[INPUT+0xac]=one_shot?0:2;
  call(f[7],starts[7],OWNER);unsigned double_accept=drain&&!one_shot;
  check(get32(OWNER+0x80)==(double_accept?6:1)&&get32(OWNER+0xb8)==(double_accept?4:0),"drain plus normal call can reuse manual mask across terminal boundary");
  check(get32(OBJ+0xa8)==(double_accept?1:0)&&get32(OBJ+0x114)==2,"text reaches exact terminal/acknowledged state");check(color_calls==1&&layout_calls==1,"terminal boundary executes color query and post-layout callback once");
  printf("terminal fixture D9=%u D8=%u one_shot=%u owner_state=%u text_state=%u status=%u\n",drain,cursor,one_shot,get32(OWNER+0x80),get32(OBJ+0xa8),get32(OWNER+0xb8));
 }
 for(unsigned state=0;state<9;state++){
  reset();put32(OWNER+0x80,state);put32(WRAP+0x9c,2);call(f[10],starts[10],WRAP);check(get32(WRAP+0x9c)==(state==6?1:2)&&timed_close_calls==0,"timed close decrements only in message state6");
 }
 const int delays[]={-1,0,1,2,30};for(unsigned k=0;k<5;k++){
  reset();put32(OWNER+0x80,6);put32(WRAP+0x9c,delays[k]);put32(WRAP+0xa0,0x12345678);unsigned firstclose=0;
  for(unsigned n=1;n<40;n++){call(f[10],starts[10],WRAP);if(timed_close_calls){firstclose=n;break;}}
  check(firstclose==(unsigned)(delays[k]>0?delays[k]+1:1),"post-completion delay checks old signed value");check(get32(WRAP+0x9c)==(u32)(delays[k]<0?delays[k]-1:-1),"expiry still decrements after close request");check(get32(WRAP+0xa0)==0x12345678,"countdown preserves initial-value copy");printf("timed subtype delay=%d close_visit=%u final_count=%d\n",delays[k],firstclose,(int)get32(WRAP+0x9c));
 }
 SnxJitStats stats;snx_jit_stats(service,&stats);snx_jit_service_free(service);printf("message guest checks=%u failures=%u compiled_functions=%lu\n",checks,failures,stats.functions);return failures?1:0;
}
