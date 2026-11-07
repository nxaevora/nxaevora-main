#include "common/cpu/recompiler/llvm/jit_service.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#define MEMSIZE 0x4300000
#define LO 0x5295f0
#define HI 0x32a30c0
#define OBJ 0x4200000
#define ANGLE 0x4201000
#define COMPONENT 0x4202000
#define REF 0x4203000
#define OUT 0x4204000
#define INPUT 0x4205000
#define STACK 0x42ff000
static u8 memory[MEMSIZE];
static GuestFn dispatch[(HI-LO)/4];
static SnxJitService *service;static RtAPI rt;static unsigned checks,failures,acquires;
static void put32(u64 a,u32 x){memcpy(memory+a,&x,4);}static u32 get32(u64 a){u32 x;memcpy(&x,memory+a,4);return x;}
static void put64(u64 a,u64 x){memcpy(memory+a,&x,8);}static void putf(u64 a,float x){memcpy(memory+a,&x,4);}
#include "turn-fixtures.h"
static void check(int ok,const char*s){checks++;if(!ok){failures++;if(failures<40)fprintf(stderr,"FAIL %s\n",s);}}
static void miss(CPU*c,u64 pc){if(pc==0){c->pc=0;return;}if(pc==0x8c69bc){acquires++;put64(c->x[8],COMPONENT);c->pc=c->x[30];return;}GuestFn f=snx_jit_wait(service,pc);if(f){f(c,pc);return;}fprintf(stderr,"unexpected miss %llx\n",(unsigned long long)pc);failures++;c->pc=0;}
static void undef(CPU*c,u64 pc,u32 w){fprintf(stderr,"undefined %x at %llx\n",w,(unsigned long long)pc);failures++;c->pc=0;}
static void poll(CPU*c){c->poll=0;}static void report(const SnxJitReport*r){if(r->error)fprintf(stderr,"JIT %s\n",r->error);}
static CPU cpu(void){CPU c={0};c.mem=memory;c.rt=&rt;c.disp=dispatch;c.code_lo=LO;c.code_hi=HI;c.x[31]=STACK;return c;}
static void run(CPU*c,u64 p){GuestFn f=snx_jit_wait(service,p);if(!f){fprintf(stderr,"no function %llx\n",(unsigned long long)p);failures++;return;}f(c,p);}
static void init(int variant,int n,float degrees,float*q){CPU c=cpu();c.x[19]=OBJ;c.x[0]=ANGLE;put32(OBJ+(variant?0x130:0x140),n);put32(OBJ+(variant?0x134:0x144),99);putf(ANGLE,degrees);putf(OBJ+0x138,degrees);run(&c,variant?0xb335ec:0xb32a78);memcpy(q,memory+OBJ+(variant?0x140:0x150),16);check(get32(OBJ+(variant?0x134:0x144))==0,"original init clears elapsed");check(c.pc==0,"original init returns");}
static void motion(int variant,const float*cur,const float*step,float*out){memcpy(memory+STACK,cur,16);memcpy(memory+OBJ+(variant?0x140:0x150),step,16);CPU c=cpu();c.x[20]=OBJ;c.x[21]=OBJ;run(&c,variant?0xb33ad4:0xb32e34);memcpy(out,memory+STACK+0x10,16);check(c.pc==0,"original motion returns");}
static int ending(int variant,int n,int elapsed,unsigned delta){put32(OBJ+(variant?0x130:0x140),n);put32(OBJ+(variant?0x134:0x144),elapsed);CPU c=cpu();c.x[20]=OBJ;c.x[0]=delta;run(&c,variant?0xb33aa0:0xb32e00);c=cpu();c.x[20]=OBJ;c.x[19]=OUT;c.x[28]=0;run(&c,variant?0xb33c60:0xb32fc0);check(c.pc==0,"original completion slice returns");return get32(OUT);}
static int setter(const float*forward,float*q){memcpy(memory+INPUT,forward,16);put64(COMPONENT+0x58,REF);put32(REF+0xc,2);memory[COMPONENT+0xe0]=0;CPU c=cpu();c.x[0]=OBJ;c.x[1]=INPUT;unsigned before=acquires;run(&c,0x19853a8);check(c.pc==0&&c.x[31]==STACK,"full original direction setter returns with balanced stack");memcpy(q,memory+COMPONENT+0xc0,16);if(acquires>before)check(get32(REF+0xc)==1,"original component release executes");return acquires>before;}
static void qyaw(double radians,float*q){q[0]=q[2]=0;q[1]=(float)sin(radians/2);q[3]=(float)cos(radians/2);}
static double yaw(const float*q){return 2*atan2(q[1],q[3]);}
static double diff(double a,double b){return remainder(a-b,6.2831853071795864769);}
int main(void){
 const u64 pcs[]={0xb32a78,0xb335ec,0xb32e34,0xb33ad4,0xb32e00,0xb33aa0,0xb32fc0,0xb33c60,0x19853a8,0xbd4380,0xbd43a4,0x32a30b0,0x5295f0};
 const size_t lengths[]={sizeof initkey_code,sizeof initturn_code,sizeof motionkey_code,sizeof motionturn_code,sizeof countkey_code,sizeof countturn_code,sizeof endkey_code,sizeof endturn_code,sizeof direction_code,sizeof success_code,sizeof running_code,sizeof acosplt_code,sizeof acos_code};
 const u8*bodies[]={initkey_code,initturn_code,motionkey_code,motionturn_code,countkey_code,countturn_code,endkey_code,endturn_code,direction_code,success_code,running_code,acosplt_code,acos_code};SnxCodeImage images[13]={0};
 for(int i=0;i<13;i++){memcpy(memory+pcs[i],bodies[i],lengths[i]);images[i].image=memory;images[i].image_size=MEMSIZE;images[i].text_off=pcs[i];images[i].text_size=lengths[i];}
 memcpy(memory+0x3788000,turn_constants,sizeof turn_constants);install_constants();put32(0x40e7bf8,1);put32(0x40e7c04,4);
 SnxJitConfig config={0};config.images=images;config.image_count=13;config.dispatch=dispatch;config.code_lo=LO;config.code_hi=HI;config.threads=1;config.opt='2';config.report=report;rt.miss=miss;rt.undef=undef;rt.poll=poll;char error[512]={0};service=snx_jit_service_new(&config,error,sizeof error);if(!service){fprintf(stderr,"startup %s\n",error);return 2;}
 const int ns[]={1,2,7,30,100};const float angles[]={-720,-181,-90,-1,0,.04f,1,90,181,720};
 for(int v=0;v<2;v++)for(int i=0;i<5;i++)for(int j=0;j<10;j++){float q[4],expect[4];init(v,ns[i],angles[j],q);qyaw(-angles[j]*3.1415927410125732/180/ns[i],expect);for(int k=0;k<4;k++)check(fabs(q[k]-expect[k])<3e-6,"original step is negative-Y authored degrees divided by frames");}
 for(int v=0;v<2;v++)for(int n=1;n<=30;n+=29)for(int e=0;e<=n+1;e++)for(unsigned d=0;d<=4;d++){int status=ending(v,n,e,d);check(status==((e+(int)d>=n)?1:4),"post-increment signed >= selects completion in same callback");}
 for(int v=0;v<2;v++){check(ending(v,-1,0,0)==1,"negative duration takes completion after motion path");check(ending(v,1,0x7fffffff,1)==4,"elapsed integer wrap preserves signed comparison (invalid-domain characterization)");}
 const float starts[]={0,.001f,.01f,.1f,1,45,90,179.99f,180,-.01f,-45};
 for(int i=0;i<11;i++){float rad=starts[i]*3.14159265358979323846/180,f[4]={sinf(rad),0,cosf(rad),0},q[4];check(setter(f,q),"valid horizontal direction is stored");check(fabs(q[0])+fabs(q[2])<1e-6,"setter stores pure Y quaternion");printf("SETTER input_degrees=%g output_degrees=%.9f\n",starts[i],yaw(q)*180/3.14159265358979323846);}
 {float q[4],f[4]={0,1,0,0};check(!setter(f,q),"vertical-only forward is rejected before component acquisition");}
 for(int v=0;v<2;v++)for(int hz=30;hz<=120;hz*=2){
  for(int mode=0;mode<3;mode++){
   float q[4]={0,0,0,1},step[4],f[4];double progress=0;unsigned calls=0;const int n=30;const float degrees=90;
   init(v,n,degrees,step);
   while(progress<n&&calls<200){double advance=mode==0?1:30.0/hz;if(advance>n-progress)advance=n-progress;if(mode==2)init(v,n,(float)(degrees*advance),step);motion(v,q,step,f);check(setter(f,q),"progress simulation stores orientation");progress+=advance;calls++;}
   double got=yaw(q)*180/3.14159265358979323846;
   if(mode==2)check(fabs(diff(yaw(q),-3.14159265358979323846/2))<0.002,"fractional motion and counter finish at authored angle");
   printf("SIM variant=%s hz=%d mode=%s calls=%u nominal_seconds=%.6f final_degrees=%.6f\n",v?"Turn":"TurnKey",hz,mode==0?"original-one-step-and-count":mode==1?"elapsed-count-only":"elapsed-counter-plus-angle",calls,(double)calls/hz,got);
  }
 }
 // One non-divisible final step: 7 frames with 4 authored frames per invocation.
 for(int v=0;v<2;v++){float q[4]={0,0,0,1},step[4],f[4];double p=0;unsigned calls=0;while(p<7){double a=fmin(4,7-p);init(v,7,(float)(90*a),step);motion(v,q,step,f);check(setter(f,q),"clamped hitch stores orientation");p+=a;calls++;}check(calls==2&&fabs(diff(yaw(q),-3.14159265358979323846/2))<.002,"clamp final 4-frame sample to remaining3");printf("HITCH variant=%d n=7 advances=4,3 final_degrees=%.6f\n",v,yaw(q)*180/3.14159265358979323846);}
 // Preserve direction and angle winding: q of 720 degrees in one frame is nearly identity,
 // but a quarter authored step must still traverse -180 degrees.
 for(int v=0;v<2;v++){float q[4],part[4];init(v,1,720,q);init(v,1,180,part);check(fabs(q[1])<2e-6&&fabs(part[1])>.99,"stored quaternion loses winding information needed for fractional turn");}
 // Small authored turn, repeated through the actual direction setter. Mode1 retains
 // the unquantized commanded yaw and only reanchors when external orientation changes.
 for(int hz=30;hz<=120;hz*=2)for(int residual=0;residual<2;residual++){
  float q[4]={0,0,0,1},step[4],f[4];double progress=0,command=0;unsigned calls=0;
  while(progress<30&&calls<200){double a=fmin(30.0/hz,30-progress);init(1,30,(float)a,step);
   if(residual){command-=3.14159265358979323846/180/30*a;float ideal[4];qyaw(command,ideal);float identity[4]={0,0,0,1};motion(1,identity,ideal,f);}else motion(1,q,step,f);
   check(setter(f,q),"small-turn direction accepted");progress+=a;calls++;
  }
  printf("SMALL hz=%d mode=%s authored_degrees=1 final_degrees=%.9f\n",hz,residual?"retained-command":"increment-current",yaw(q)*180/3.14159265358979323846);
  if(residual)check(fabs(diff(yaw(q),-3.14159265358979323846/180))<.0001,"retained commanded yaw eventually crosses acos quantization");
  if(hz==120&&!residual)check(fabs(yaw(q))<1e-9,"sub-quantum incremental direction can stall at forward");
 }
 // External orientation changes must be honored. The proposed command follows
 // +0.2 rad motion, an injected +0.4 rad external yaw, then +0.2 rad motion.
 {float q[4],f[4],step[4];qyaw(.2,q);qyaw(.2,step);qyaw(.6,q);motion(1,q,step,f);check(setter(f,q),"external orientation reanchor direction accepted");check(fabs(diff(yaw(q),.8))<.0001,"increment from observed orientation preserves external yaw change");}
 // Proposed local quantization residual: requested=observed+residual+ownAdvance;
 // residual=requested-actualWritten. External edits remain in observed orientation.
 for(int inject=0;inject<2;inject++){
  float q[4]={0,0,0,1},f[4],step[4];double residual=0,p=0;unsigned calls=0;
  while(p<30){double a=fmin(.25,30-p);if(inject&&calls==60)qyaw(yaw(q)+.4,q);double own=-3.14159265358979323846/180/30*a,requested=yaw(q)+residual+own;
   qyaw(own+residual,step);motion(1,q,step,f);check(setter(f,q),"residual design direction accepted");residual=diff(requested,yaw(q));p+=a;calls++;
  }
  double expected=-3.14159265358979323846/180+(inject?.4:0);check(fabs(diff(yaw(q),expected))<.0001,"local residual preserves authored turn and injected external yaw");
  printf("RESIDUAL external_yaw_rad=%.1f final_degrees=%.9f residual_degrees=%.9f\n",inject?.4:0,yaw(q)*180/3.14159265358979323846,residual*180/3.14159265358979323846);
 }
 SnxJitStats stats;snx_jit_stats(service,&stats);snx_jit_service_free(service);printf("Turn guest checks=%u failures=%u compiled_functions=%lu\n",checks,failures,stats.functions);return failures?1:0;
}

