#include "common/cpu/recompiler/llvm/jit_service.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "resolution-fixtures.h"
#define MEMORY 0x4400000
#define LO CODE_START
#define HI (CODE_START+sizeof code_bytes)
#define OBJ 0x4300000
#define SERVICE 0x4301000
#define SERVICEVT 0x4302000
#define PARAMS 0x4303000
#define PARAMSVT 0x4304000
#define STACK 0x43ff000
static u8 memory[MEMORY];static GuestFn dispatch[(HI-LO)/4];
static SnxJitService* service;static RtAPI rt;
static unsigned checks,failures,writes;static float width,height;
static void check(int ok,const char*why){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",why);}}
static void put32(u64 p,u32 v){memcpy(memory+p,&v,4);}static u32 get32(u64 p){u32 v;memcpy(&v,memory+p,4);return v;}
static void put64(u64 p,u64 v){memcpy(memory+p,&v,8);}static u64 get64(u64 p){u64 v;memcpy(&v,memory+p,8);return v;}
static void putf(u64 p,float v){memcpy(memory+p,&v,4);}static float getf(u64 p){float v;memcpy(&v,memory+p,4);return v;}
static void miss(CPU*c,u64 pc){
 if(pc==0x1000){check(c->x[0]==SERVICE+0x58,"parameter provider subobject");c->x[0]=PARAMS;}
 else if(pc==0x1004){
  const char*name=(char*)(memory+get64(c->x[1]+8));float value=getf(c->x[2]);
  check(c->x[0]==PARAMS,"parameter storage receiver");
  if(!strcmp(name,"3DBufferWidthScale"))width=value;
  else if(!strcmp(name,"3DBufferHeightScale"))height=value;
  else check(0,"recognized render scale parameter");
  writes++;
 }else if(pc>=LO&&pc<HI){GuestFn f=snx_jit_wait(service,pc);if(f){f(c,pc);return;}check(0,"JIT function available");}
 else {fprintf(stderr,"Unexpected external %llx\n",(unsigned long long)pc);check(0,"known external");c->pc=0;return;}
 c->pc=c->x[30];
}
static void undef(CPU*c,u64 pc,u32 w){fprintf(stderr,"Undefined %x at%llx\n",w,(unsigned long long)pc);failures++;c->pc=0;}
static void poll(CPU*c){c->poll=0;}
static CPU cpu(void){CPU c={0};c.mem=memory;c.rt=&rt;c.disp=dispatch;c.code_lo=LO;c.code_hi=HI;c.x[31]=STACK;c.x[29]=STACK;c.x[0]=OBJ;return c;}
static void clear(int mode,int override){memset(memory+OBJ,0,0x1000);put32(OBJ,mode);put32(OBJ+0x28,override);put32(OBJ+0x40,123);putf(OBJ+0x2c,0.63f);putf(OBJ+0x30,0.72f);width=height=-1;writes=0;}
static void call(GuestFn f,u64 pc,CPU*c){f(c,pc);check(c->pc==0&&c->x[31]==STACK,"function returns with balanced stack");}
static float expected(int mode,int axis){
 switch(mode){case 2:return axis?0.85f:0.90f;case 3:return axis?0.80f:0.85f;case 4:return axis?0.70f:0.80f;
 /* Original MOV immediates 3a5ed28a / 3a6bedfb, not rounded decimal literals. */
 case 5:return axis?0x1.bda514p-11f:0x1.d7dbf6p-11f;case 6:return axis?0.72f:0.63f;case 7:return 0.5f;default:return 1.0f;}
}
int main(void){
 memcpy(memory+CODE_START,code_bytes,sizeof code_bytes);memcpy(memory+CONSTANTS_START,constants_bytes,sizeof constants_bytes);memcpy(memory+NAMES_START,names_bytes,sizeof names_bytes);
 put64(0x42ef560,SERVICE);put64(SERVICE+0x58,SERVICEVT);put64(SERVICEVT+0x28,0x1000);put64(PARAMS,PARAMSVT);put64(PARAMSVT+0x10,0x1004);
 SnxCodeImage image={0};image.image=memory;image.image_size=MEMORY;image.text_off=LO;image.text_size=HI-LO;
 SnxJitConfig cfg={0};cfg.images=&image;cfg.image_count=1;cfg.dispatch=dispatch;cfg.code_lo=LO;cfg.code_hi=HI;cfg.threads=1;cfg.opt='2';
 rt.miss=miss;rt.undef=undef;rt.poll=poll;char error[512]={0};service=snx_jit_service_new(&cfg,error,sizeof error);if(!service){fprintf(stderr,"%s\n",error);return 2;}
 GuestFn apply=snx_jit_wait(service,0xcc3888),set=snx_jit_wait(service,0xcc386c),custom=snx_jit_wait(service,0xcc3e44),reset=snx_jit_wait(service,0xcc3e58);
 if(!apply||!set||!custom||!reset)return 2;
 int overrides[]={-1,0,1,2,3,4,5,6,7,8,9,0x7fffffff};
 for(int mode=-1;mode<=9;mode++)for(unsigned j=0;j<sizeof overrides/sizeof *overrides;j++){
  int over=overrides[j],effective=over<8?over:mode;clear(mode,over);CPU c=cpu();call(apply,0xcc3888,&c);
  check(writes==2,"each application writes both scale parameters");
  if(width!=expected(effective,0)||height!=expected(effective,1))fprintf(stderr,"mode=%d override=%d effective=%d actual=%a,%a expected=%a,%a\n",mode,over,effective,(double)width,(double)height,(double)expected(effective,0),(double)expected(effective,1));
  check(width==expected(effective,0)&&height==expected(effective,1),"mode and signed override select exact original float pair");
  check(get32(OBJ)==(u32)mode&&get32(OBJ+0x28)==(u32)over,"application preserves requested mode and override");
  check(get32(OBJ+0x40)==0,"application clears pending field40");
 }
 for(int force=0;force<4;force++)for(int same=0;same<2;same++){
  clear(1,8);CPU c=cpu();c.x[1]=same?1:7;c.x[2]=force;call(set,0xcc386c,&c);
  check(writes==((!same||(force&1))?2u:0u),"mode setter reapplies only changed mode or force bit0");check(get32(OBJ)==(same?1u:7u),"mode setter stores requested mode");
 }
 clear(1,8);CPU c=cpu();c.v[0].f[0]=1.25f;c.v[1].f[0]=1.5f;call(custom,0xcc3e44,&c);
 check(get32(OBJ)==6&&width==1.25f&&height==1.5f,"custom setter mode6 applies supplied scale values");
 clear(1,7);c=cpu();c.v[0].f[0]=1.25f;c.v[1].f[0]=1.5f;call(custom,0xcc3e44,&c);
 check(get32(OBJ)==6&&getf(OBJ+0x2c)==1.25f&&width==0.5f&&height==0.5f,"active override supersedes custom value without discarding it");
 for(int mode=0;mode<=7;mode++){
  clear(mode,8);c=cpu();call(reset,0xcc3e58,&c);
  check(get32(OBJ)==1&&getf(OBJ+0x2c)==0.9f&&getf(OBJ+0x30)==0.85f,"reset restores automatic mode and stored defaults");
  check(writes==(mode==1?0u:2u),"reset only reapplies when leaving another mode");
  if(mode!=1)check(width==1&&height==1,"automatic mode reset initially restores full scale");
 }
 SnxJitStats stats;snx_jit_stats(service,&stats);snx_jit_service_free(service);
 printf("resolution guest checks=%u failures=%u compiled_functions=%lu\n",checks,failures,stats.functions);return failures?1:0;
}
