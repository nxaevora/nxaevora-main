import sys
sys.path.pop(0)
from pathlib import Path
p=Path('work/pass4-ai/make-fixtures.py');s=p.read_text();s=s.replace("('turn',0xb32e34,0xb32fbc-0xb32e34+4)","('turn',0xb32e34,0xb32fbc-0xb32e34+4),('turn2',0xb33ad4,0xb33c5c-0xb33ad4+4),('count1',0xb32e00,0x10),('count2',0xb33aa0,0x10)")
s=s.replace("if name=='turn':", "if name in ('turn','turn2','count1','count2'):")
p.write_text(s)
p=Path('work/pass4-ai/ai-jit.c');s=p.read_text();s=s.replace('#define LO 0xb32e34','#define LO 0xb32e00')
s=s.replace('0x1059a78,0xb32e34};','0x1059a78,0xb32e34,0xb33ad4,0xb32e00,0xb33aa0};')
s=s.replace('sizeof getter_code,sizeof turn_code};','sizeof getter_code,sizeof turn_code,sizeof turn2_code,sizeof count1_code,sizeof count2_code};')
s=s.replace('getter_code,turn_code};SnxCodeImage images[5]={0};','getter_code,turn_code,turn2_code,count1_code,count2_code};SnxCodeImage images[8]={0};')
s=s.replace('for(int i=0;i<5;i++){memcpy','for(int i=0;i<8;i++){memcpy').replace('config.image_count=5','config.image_count=8')
a=s.index(' const float angles[]');z=s.index(' SnxJitStats stats;',a)
s=s[:a]+''' const float angles[]={-.6f,-.2f,0,.3f,.8f};
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
 printf("Turn/TurnKey: 50 quaternion pairs tested at frame deltas 0,1,4; %u noncommuting pairs distinguish step * current order.\\n",noncommuting);
''' +s[z:]
p.write_text(s)
