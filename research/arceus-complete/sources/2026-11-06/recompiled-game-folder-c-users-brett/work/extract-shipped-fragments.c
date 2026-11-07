/* Local-only scan of the user's extracted BNSH files, using the runtime scanner. */
#include "nx/gpu/shader/shader.h"
#include "nx/gpu/shader/shipped.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WINDOW 0x20000u
static const char* output;
static const char* input;
static int failed;
static int program(void* ctx,const u8* sph,size_t avail,int stage) {
    (void)ctx;
    if (stage!=ST_FS) return 0;
    u8* code=calloc(1,WINDOW);
    if (!code) {failed=1;return 1;}
    memcpy(code,sph,avail<WINDOW?avail:WINDOW);
    u32 len=0;u64 hash=shader_hash(code,WINDOW,&len);
    ShaderInfo si={0};
    if (!shader_corpus_save(output,code,len,WINDOW,stage,&si,hash)) failed=1;
    printf("%016llx %u %s\n",(unsigned long long)hash,len,input);
    free(code);return failed;
}
int main(int argc,char** argv) {
    if (argc<3)return 2;
    output=argv[1];
    for (int i=2;i<argc && !failed;i++) {
        input=argv[i];FILE* f=fopen(input,"rb");
        if (!f)return 2;
        fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
        if (n<0 || n>(64<<20)) {fclose(f);return 2;}
        u8* bytes=malloc(n?n:1);
        if (!bytes || fread(bytes,1,n,f)!=(size_t)n) {free(bytes);fclose(f);return 2;}
        fclose(f);shipped_programs(bytes,n,program,NULL);free(bytes);
    }
    return failed;
}
