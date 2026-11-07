#include "common/cpu/recompiler/llvm/jit_service.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "event-fixtures.h"
#define MEMORY 0x4300000
#define LO 0x167fc90
#define HI 0x1683884
#define OBJ 0x4200000
#define NAV 0x4201000
#define CHAR1 0x4202000
#define CHAR2 0x4203000
#define REFS 0x4204000
#define INIT 0x4205000
#define SLOT 0x4205100
#define MIRROR 0x4205200
#define FADE 0x4205300
#define STACK 0x42ff000
static u8 memory[MEMORY];static GuestFn dispatch[(HI-LO)/4];
static SnxJitService* service;static RtAPI rt;
static unsigned checks,failures,out_calls,in_calls,end_calls,stop_calls,place_calls,path_calls,control_calls;
static unsigned ended,placed,pathfinding,preferred,control_value,missing_ref,missing_nav;
static int slice_exit_active;
static void check(int ok,const char* why){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",why);}}
static void put32(u64 p,u32 v){memcpy(memory+p,&v,4);}static u32 get32(u64 p){u32 v;memcpy(&v,memory+p,4);return v;}
static void put64(u64 p,u64 v){memcpy(memory+p,&v,8);}static float getf(u64 p){float v;memcpy(&v,memory+p,4);return v;}
static void putf(u64 p,float v){memcpy(memory+p,&v,4);}
static void miss(CPU*c,u64 pc){
 if(pc==0&&slice_exit_active){c->pc=0;return;} /* BR X28 extraction sentinel */
 switch(pc){
 case 0xae5694:put64(c->x[8],missing_ref?0:c->x[0]==OBJ+0x70?CHAR1:CHAR2);break;
 case 0x9c64cc:put64(c->x[8],missing_nav?0:NAV);break;
 case 0xc264cc:out_calls++;check(c->x[0]==FADE&&c->x[1]==0&&c->x[5]==30,"fallback requests channel0 fade out for30frames");break;
 case 0xc26428:end_calls++;c->x[0]=ended;break;
 case 0xc265c8:in_calls++;check(c->x[0]==FADE&&c->x[1]==0&&c->x[2]==30,"fallback requests channel0 fade in for30frames");break;
 case 0x171b198:stop_calls++;check(c->x[0]==NAV,"stop uses first character navigator");break;
 case 0x198af40:place_calls++;check(c->x[0]==CHAR1&&c->x[1]==OBJ+0x1b0,"placement targets first character and cached vector");c->x[0]=placed;break;
 case 0x171ae88:path_calls++;c->x[0]=pathfinding;break;
 case 0x168115c:control_calls++;control_value=c->x[1];break;
 case 0x171d458:preferred=c->x[1];break;
 default:
   if(pc>=LO&&pc<HI){GuestFn f=snx_jit_wait(service,pc);if(f){f(c,pc);return;}}
   fprintf(stderr,"unexpected external call %llx\n",(unsigned long long)pc);failures++;c->pc=0;return;
 }
 c->pc=c->x[30];
}
static void undef(CPU*c,u64 pc,u32 w){fprintf(stderr,"undefined %x at%llx\n",w,(unsigned long long)pc);failures++;c->pc=0;}
static void poll(CPU*c){c->poll=0;}static void report(const SnxJitReport*r){if(r->error)fprintf(stderr,"JIT %s\n",r->error);}
static CPU cpu(void){CPU c={0};c.mem=memory;c.rt=&rt;c.disp=dispatch;c.code_lo=LO;c.code_hi=HI;c.x[31]=STACK;c.x[29]=STACK;return c;}
static void clear(void){
 memset(memory+OBJ,0,0x1000);put64(OBJ+0x80,1);
 put64(NAV+0x58,REFS);put64(CHAR1+0x58,REFS+0x100);put64(CHAR2+0x58,REFS+0x200);
 put32(REFS+0xc,1000);put32(REFS+0x10c,1000);put32(REFS+0x20c,1000);
 out_calls=in_calls=end_calls=stop_calls=place_calls=path_calls=control_calls=0;
 ended=placed=pathfinding=1;preferred=control_value=99;missing_ref=missing_nav=0;
}
static void call(GuestFn f,u64 pc){CPU c=cpu();c.x[0]=OBJ;f(&c,pc);check(c.pc==0&&c.x[31]==STACK,"full function returns with balanced stack");}
static void recovery(GuestFn f,u64 pc){CPU c=cpu();c.x[19]=NAV;c.x[20]=OBJ;slice_exit_active=1;f(&c,pc);slice_exit_active=0;check(c.pc==0&&c.x[31]==STACK,"timer slice reaches extraction continuation");}
int main(void){
 const u64 starts[]={0x167fc90,0x16832a4,0x1683528,0x1682f70};
 const u8*bodies[]={wait_code,recovery_code,fallback_code,reset_code};
 const size_t lengths[]={sizeof wait_code,sizeof recovery_code,sizeof fallback_code,sizeof reset_code};
 SnxCodeImage images[4]={0};for(int i=0;i<4;i++){memcpy(memory+starts[i],bodies[i],lengths[i]);images[i].image=memory;images[i].image_size=MEMORY;images[i].text_off=starts[i];images[i].text_size=lengths[i];}
 memcpy(memory+0x378a290,fade_constant,sizeof fade_constant);memcpy(memory+0x397c590,fallback_jump,sizeof fallback_jump);
 put64(0x4279e28,INIT);put32(INIT,1);put64(0x4279e30,SLOT);put64(SLOT,FADE);put64(0x4279e38,MIRROR);
 SnxJitConfig cfg={0};cfg.images=images;cfg.image_count=4;cfg.dispatch=dispatch;cfg.code_lo=LO;cfg.code_hi=HI;cfg.threads=1;cfg.opt='2';cfg.report=report;
 rt.miss=miss;rt.undef=undef;rt.poll=poll;char error[512]={0};service=snx_jit_service_new(&cfg,error,sizeof error);if(!service){fprintf(stderr,"startup%s\n",error);return 2;}
 GuestFn wait=snx_jit_wait(service,0x167ff0c),resetwait=snx_jit_wait(service,0x167fc90),near=snx_jit_wait(service,0x16832a4),far=snx_jit_wait(service,0x16832d4),fallback=snx_jit_wait(service,0x1683528),reset=snx_jit_wait(service,0x1682f70);
 if(!wait||!resetwait||!near||!far||!fallback||!reset)return 2;
 const float step=0x1.111112p-5f;const float limits[]={0,step,0.1f,1,2,6};
 for(int j=0;j<6;j++)for(int k=0;k<5;k++){
  float t=limits[j],v=k==0?0:k==1?nextafterf(t,-INFINITY):k==2?t:k==3?nextafterf(t,INFINITY):t+0.5f;
  clear();putf(OBJ+0x190,v);put32(OBJ+0x194,0xaabbccdd);CPU c=cpu();c.x[19]=OBJ;c.v[8].f[0]=t;wait(&c,0x167ff0c);float expected=v+step;if(t<expected)expected=t;
  check(getf(OBJ+0x190)==expected,"wait increment saturates at configured threshold");check(get32(OBJ+0x194)==0xaabbccdd,"wait slice leaves adjacent state intact");
  c=cpu();c.x[19]=OBJ;resetwait(&c,0x167fc90);check(getf(OBJ+0x190)==0,"false predicate continuation resets wait timer");
 }
 clear();putf(OBJ+0x1a0,0);putf(OBJ+0x198,0);unsigned firststop=0;
 for(unsigned n=1;n<=62;n++){recovery(near,0x16832a4);if(stop_calls&&!firststop)firststop=n;check(out_calls==0,"two-second boundary remains before overall fallback");}
 check(firststop==62,"binary32 two-second old-value test first stops on eligible call62");printf("in-range first_stop_call=%u timer=%.9g\n",firststop,getf(OBJ+0x1a0));
 unsigned stops=stop_calls;float remembered=getf(OBJ+0x1a0);recovery(far,0x16832d4);check(getf(OBJ+0x1a0)==remembered&&stop_calls==stops,"far-path timer continuation neither resets nor advances in-range timer");
 clear();unsigned firstout=0;
 for(unsigned n=1;n<=182;n++){recovery(far,0x16832d4);if(out_calls){firstout=n;break;}check(path_calls==n,"before six-second expiry returns navigator pathfinding state");}
 check(firstout==182&&get32(OBJ+0x194)==1,"binary32 six-second timeout enters fade substate on call182");printf("recovery first_fade_out_call=%u total=%.9g\n",firstout,getf(OBJ+0x198));
 for(unsigned state=0;state<=5;state++)for(unsigned done=0;done<=1;done++){
  clear();ended=done;put32(OBJ+0x194,state);putf(OBJ+0x19c,1);putf(OBJ+0x198,8);call(fallback,0x1683528);
  unsigned expected=state==0?1:state==1&&done?2:state==2?3:state==3&&done?4:state;
  check(get32(OBJ+0x194)==expected,"fallback preserves exact entry-state transition");
  check(getf(OBJ+0x19c)==(state==2?1+step:1),"only fallback entry state2 increments its clock");
  check(getf(OBJ+0x198)==8,"fallback does not itself change overall timer");
  check(place_calls==(unsigned)(state==1&&done),"reposition only after fade-out completion");
 }
 clear();put32(OBJ+0x194,1);placed=0;call(fallback,0x1683528);check(get32(OBJ+0x194)==2&&place_calls==1,"placement failure return does not prevent state2 transition");
 clear();put32(OBJ+0x194,1);missing_ref=1;call(fallback,0x1683528);check(get32(OBJ+0x194)==1&&place_calls==0,"missing character prevents placement and state2 transition");
 clear();put32(OBJ+0x194,2);unsigned firstin=0;
 for(unsigned n=1;n<=31;n++){call(fallback,0x1683528);if(in_calls){firstin=n;break;}check(get32(OBJ+0x194)==2,"hold waits in state2 before old-value threshold");}
 check(firstin==31&&get32(OBJ+0x194)==3,"one-second hold initiates fade in on state2 visit31");printf("hold first_fade_in_call=%u hold=%.9g\n",firstin,getf(OBJ+0x19c));
 clear();put32(OBJ+0x194,4);putf(OBJ+0x198,6);recovery(far,0x16832d4);check(getf(OBJ+0x198)==0&&get32(OBJ+0x194)==4,"completed fallback resets total timer and preserves state4");
 clear();for(unsigned p=0x190;p<=0x1a0;p+=4)put32(OBJ+p,0xdeadbeef);put64(OBJ+0x1b0,~0ull);put64(OBJ+0x1b8,~0ull);call(reset,0x1682f70);
 check(get32(OBJ+0x190)==0xdeadbeef,"episode reset does not reset separate wait timer");
 for(unsigned p=0x194;p<=0x1a0;p+=4)check(get32(OBJ+p)==0,"episode reset clears state and all three recovery timers");
 check(control_calls==1&&control_value==1&&preferred==2,"recovery cleanup restores player control and preferred state2");
 for(unsigned null_case=0;null_case<2;null_case++){
  clear();for(unsigned p=0x190;p<=0x1a0;p+=4)put32(OBJ+p,0xdeadbeef);put64(OBJ+0x1b0,~0ull);put64(OBJ+0x1b8,~0ull);
  if(null_case==0)put64(OBJ+0x80,0);else missing_nav=1;
  call(reset,0x1682f70);
  for(unsigned p=0x190;p<=0x1a0;p+=4)check(get32(OBJ+p)==0xdeadbeef,"missing first navigator leaves wait/state/recovery clocks unchanged");
  for(unsigned p=0x1b0;p<=0x1bc;p+=4)check(get32(OBJ+p)==0xffffffff,"missing first navigator leaves cached position unchanged");
  check(stop_calls==0&&control_calls==0&&preferred==99,"missing first navigator bypasses cleanup side effects");
 }
 SnxJitStats stats;snx_jit_stats(service,&stats);snx_jit_service_free(service);printf("event guest checks=%u failures=%u compiled_functions=%lu\n",checks,failures,stats.functions);return failures?1:0;
}
