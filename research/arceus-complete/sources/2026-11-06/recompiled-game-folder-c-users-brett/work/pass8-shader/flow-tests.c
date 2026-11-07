#include "nx/gpu/shader/translator.h"
int pass8_screen_probe(const u64*,u32,u32,u32,u32);
int pass8_reach_probe(const u64*,u32,u32,u32);
int pass8_def_probe(const u64*,u32,u32,u32);
static int failures,cases;
static u64 field(u64 w,int at,int bits,u64 v){u64 m=((1ull<<bits)-1)<<at;return(w&~m)|(v<<at);}
static u64 ipa(int axis,int d){return field(field(0xe003ff870ff7ff00ull,30,8,28+axis),0,8,d);}
static u64 f2i(int d,int s){return field(field(0x5cb0118000070a00ull,20,8,s),0,8,d);}
static u64 mov(int d,int s){return field(field(0x5c9807800ff70000ull,20,8,s),0,8,d);}
static u64 branch(u64 base,int from,int to){return field(base,20,24,(u32)((to-from-1)*8)&0xffffff);}
static void base(u64 w[64]){
 memset(w,0,64*8);
 for(int i=10;i<64;i++)w[i]=IDX_OK(i)?0x50b0000000070f00ull:0x001f8000fc0007e0ull;
 w[11]=ipa(0,2);w[12]=ipa(1,3);w[13]=f2i(2,2);w[15]=f2i(3,3);
 w[16]=0xe33000000000000full; /* @P0 KIL */
 w[17]=branch(0xe290000000000000ull,17,25);
 w[19]=mov(10,255); w[20]=0xf0f800000008000full; /* @!P0 SYNC */
 w[21]=mov(10,255); w[23]=0xf0f800000007000full;
 w[25]=0xda40010ff037020aull; /* TLDS 2D xR2 yR3 ->R10 */
 w[27]=0xe30000000007000full;
}
static void test(const char* name,u64* w,int n,int use,int expected,int old_expected,int old){
 int got=pass8_screen_probe(w,n,use,2,3);
 int want=old?old_expected:expected;
 printf("%s: screen=%d expected=%d defX=%d defY=%d\n",name,got,want,pass8_def_probe(w,n,use,2),pass8_def_probe(w,n,use,3));
 cases++;if(got!=want)failures++;
}
static void real(int old,const char* out){
 const char* file="work/shipped-fragment-corpus/e9e7255e7594a006_4.snxs";
 FILE* f=fopen(file,"rb");u32 h[6];ShaderInfo info;
 if(!f||fread(h,4,6,f)!=6||h[2]!=sizeof info||fread(&info,1,sizeof info,f)!=sizeof info){puts("load failed");failures++;if(f)fclose(f);return;}
 u64* w=malloc(h[5]);if(fread(w,1,h[4],f)!=h[4]){failures++;fclose(f);free(w);return;}fclose(f);
 printf("real e9: reach16to61=%d reach17to61=%d defX=%d defY=%d fetch61=%d\n",pass8_reach_probe(w,h[4]/8,16,61),pass8_reach_probe(w,h[4]/8,17,61),pass8_def_probe(w,h[4]/8,61,2),pass8_def_probe(w,h[4]/8,61,7),pass8_screen_probe(w,h[4]/8,61,2,7));
 cases++; if(pass8_screen_probe(w,h[4]/8,61,2,7)!=(old?0:1))failures++;
 for(int r=2;r<=7;r+=5)for(int i=r==2?16:17;i<61;i++)if(IDX_OK(i)){const Op* op=decode(w[i]);if(!op||shader_writes_reg(w[i],op->id,r)){printf("unexpected coordinate writer: %d R%d\n",i,r);failures++;}}
 g_shader_screen_fetch=1;g_shader_texture_arrays=1;g_shader_maximal_reconvergence=1;
 size_t nw=0;u32* spv=shader_to_spirv((u8*)w,h[4],ST_FS,&info,&nw,1,1,1,1);
 if(!spv){printf("translate failed %s\n",info.first_unsupported);failures++;}else{
 char path[2048];snprintf(path,sizeof path,"%s/e9-%s.spv",out,old?"original":"fixed");FILE* s=fopen(path,"wb");if(!s||fwrite(spv,4,nw,s)!=nw)failures++;if(s)fclose(s);free(spv);}
 free(w);
}
int main(int argc,char**argv){
 int old=argc>1&&atoi(argv[1]);u64 w[64];
 base(w);test("conditional-kill-ssy-sync",w,28,25,1,0,old);
 base(w);w[16]=field(w[16],16,4,8);test("inverse-conditional-kill",w,28,25,1,0,old);
 base(w);w[16]=field(w[16],16,4,7);test("unconditional-kill",w,28,25,0,0,old);
 base(w);w[16]=0x50b0000000070f00ull;test("ssy-sync-no-kill",w,28,25,1,1,old);
 base(w);w[21]=field(mov(2,255),16,4,0);test("conditional-coordinate-write",w,28,25,0,0,old);
 base(w);w[21]=mov(2,255);test("coordinate-write",w,28,25,0,0,old);
 base(w);w[11]=branch(0xe24000000000000full,11,17);w[12]=ipa(0,2);w[13]=ipa(1,3);w[15]=f2i(2,2);w[16]=f2i(3,3);test("branch-skips-initialization",w,28,25,0,0,old);
 base(w);w[27]=0x50b0000000070f00ull;w[28]=mov(2,255);w[29]=branch(0xe24000000000000full,29,25);w[31]=0xe30000000007000full;test("backedge-changes-coordinate",w,32,25,0,0,old);
 base(w);w[19]=branch(0xe260000000000000ull,19,31);w[31]=mov(2,255);w[32]=0xe32000000007000full;w[33]=0xe30000000007000full;test("call-has-unknown-return",w,34,25,0,0,old);
 base(w);w[19]=0xe21000000007000full;test("indirect-jump-unknown",w,28,25,0,0,old);
 real(old,"work/pass8-shader");
 printf("pass8-flow: %d cases, %d failures (%s)\n",cases,failures,old?"original":"fixed");return failures?1:0;
}

