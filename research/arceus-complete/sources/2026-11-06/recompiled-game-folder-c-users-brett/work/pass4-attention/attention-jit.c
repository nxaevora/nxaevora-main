#include "common/cpu/recompiler/llvm/jit_service.h"
#include <stdio.h>
#include <string.h>
#include "attention-fixtures.h"
#define MEMORY 0x20000
#define LO 0x1000
#define HI 0x8000
#define OBJ 0xa000
static u8 memory[MEMORY];
static GuestFn dispatch[(HI-LO)/4];
static RtAPI rt;
static unsigned checks,failures,restores[4];
static void check(int ok,const char* why){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",why);}}
static void put32(u64 p,u32 v){memcpy(memory+p,&v,4);}
static u32 get32(u64 p){u32 v;memcpy(&v,memory+p,4);return v;}
static void put64(u64 p,u64 v){memcpy(memory+p,&v,8);}
static void restore(CPU* c,u64 pc){
    (void)pc;u64 slot=c->x[1];
    check(slot<4&&c->x[0]==OBJ&&c->x[2]==OBJ+0x3e0+slot*0x80,"look expiration restore arguments");
    if(slot<4)restores[slot]++;
    c->x[0]=0xcafe;c->pc=c->x[30];
}
static void miss(CPU* c,u64 pc){fprintf(stderr,"unexpected external call %llx\n",(unsigned long long)pc);failures++;c->pc=0;}
static void undef(CPU* c,u64 pc,u32 w){fprintf(stderr,"undefined %x at %llx\n",w,(unsigned long long)pc);failures++;c->pc=0;}
static void poll(CPU* c){c->poll=0;}
static void report(const SnxJitReport* r){if(r->error)fprintf(stderr,"JIT %s\n",r->error);}
static CPU cpu(void){CPU c={0};c.mem=memory;c.rt=&rt;c.disp=dispatch;c.code_lo=LO;c.code_hi=HI;c.x[31]=0x1f000;c.x[0]=OBJ;return c;}
static void reset_object(void){memset(memory+OBJ,0,0x800);put64(OBJ,0x9000);put64(0x9100,0x7000);memset(restores,0,sizeof restores);}
static void call(GuestFn fn,u64 pc){CPU c=cpu();for(unsigned r=19;r<30;r++)c.x[r]=0x123400+r;fn(&c,pc);
    int saved=1;for(unsigned r=19;r<30;r++)saved&=c.x[r]==0x123400+r;
    check(c.pc==0&&c.x[31]==0x1f000&&saved,"original update balances stack and preserves saved registers");}
int main(void){
    memcpy(memory+0x1000,look_code,sizeof look_code);memcpy(memory+0x5000,face_prefix_code,sizeof face_prefix_code);
    dispatch[(0x7000-LO)/4]=restore;
    SnxCodeImage images[2]={0};const u64 starts[]={0x1000,0x5000};const size_t sizes[]={sizeof look_code,sizeof face_prefix_code};
    for(unsigned i=0;i<2;i++){images[i].image=memory;images[i].image_size=MEMORY;images[i].text_off=starts[i];images[i].text_size=sizes[i];}
    SnxJitConfig config={0};config.images=images;config.image_count=2;config.dispatch=dispatch;config.code_lo=LO;config.code_hi=HI;config.threads=1;config.opt='2';config.report=report;
    rt.miss=miss;rt.undef=undef;rt.poll=poll;char error[512]={0};SnxJitService* service=snx_jit_service_new(&config,error,sizeof error);
    if(!service){fprintf(stderr,"startup %s\n",error);return 2;}
    GuestFn look=snx_jit_wait(service,0x1000),face=snx_jit_wait(service,0x5000);
    if(!look||!face){snx_jit_service_free(service);return 2;}
    const int values[]={-2,-1,0,1,2,30,120};
    for(unsigned slot=0;slot<4;slot++)for(unsigned v=0;v<7;v++){
        reset_object();const unsigned active=0x3d0+slot*0x80,timer=0x440+slot*0x80;
        memory[OBJ+active]=1;put32(OBJ+timer,values[v]);memory[OBJ+0x218]=1;
        call(look,0x1000);
        check((int)get32(OBJ+timer)==values[v]-1,"look slot decrements once even from zero");
        check(memory[OBJ+active]==(values[v]>1),"look hold active flag follows signed expiration");
        check(restores[slot]==(values[v]<=1),"look expiration invokes exactly one restore");
        check(memory[OBJ+0x218]==0,"non-timer look state still updates");
        for(unsigned other=0;other<4;other++)if(other!=slot)check(restores[other]==0,"other look slots remain independent");
        if(values[v]<=1){call(look,0x1000);check(restores[slot]==1,"expired hold does not restore again");}
    }
    reset_object();
    for(unsigned slot=0;slot<4;slot++){memory[OBJ+0x3d0+slot*0x80]=1;put32(OBJ+0x440+slot*0x80,slot+1);}
    for(unsigned step=1;step<=5;step++){
        call(look,0x1000);
        for(unsigned slot=0;slot<4;slot++)check(restores[slot]==(step>=slot+1),"simultaneous look holds expire independently");
    }
    const int starts_count[]={0,1,9999998,9999999,10000000};
    for(unsigned active=0;active<2;active++)for(unsigned i=0;i<5;i++){
        reset_object();memory[OBJ+0x5f0]=active;put32(OBJ+0x670,starts_count[i]);call(look,0x1000);
        check(get32(OBJ+0x670)==(unsigned)(starts_count[i]+(active&&starts_count[i]<9999999)),"look-start count respects active flag and saturation");
    }
    for(unsigned active=0;active<2;active++)for(unsigned i=0;i<7;i++){
        reset_object();memory[OBJ+0x258]=active;put32(OBJ+0x280,values[i]);call(face,0x5000);
        check((int)get32(OBJ+0x280)==values[i]-(int)active,"face hold decrements only while active");
        check(memory[OBJ+0x258]==(active&&values[i]>1),"face hold expiration disables override");
    }
    for(unsigned i=0;i<7;i++){
        reset_object();put32(OBJ+0x2a4,values[i]);call(face,0x5000);
        check((int)get32(OBJ+0x2a4)==(values[i]>0?values[i]-1:values[i]),"vigilance delay stores only nonnegative decrement result");
    }
    const unsigned flags[]={0,1,2,3,4,8,255};
    for(unsigned i=0;i<7;i++){
        reset_object();memory[OBJ+0x250]=flags[i];memory[OBJ+0x258]=1;put32(OBJ+0x280,2);put32(OBJ+0x2a4,2);call(face,0x5000);
        unsigned expected=flags[i]&3?2:1;
        check(get32(OBJ+0x280)==expected&&get32(OBJ+0x2a4)==expected,"face update low-two-bit gate freezes both clocks");
    }
    SnxJitStats stats;snx_jit_stats(service,&stats);snx_jit_service_free(service);
    printf("attention instruction checks=%u failures=%u compiled_functions=%lu\n",checks,failures,stats.functions);
    return failures?1:0;
}
