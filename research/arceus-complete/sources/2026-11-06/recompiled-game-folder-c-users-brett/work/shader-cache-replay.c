/* Local test adapter: replay captured constant-buffer reads so BRX jump tables
   in the existing cache can translate offline without running the game. */
#include "nx/gpu/shader/shader.h"
#include "common/utilities/endian.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static u32 reads[16384];
static size_t nreads;
static int replay_read(u32 bank, u32 offset, u32* out) {
    for (size_t i=0;i<nreads;i+=4) {
        if (reads[i]!=bank || reads[i+1]!=offset) continue;
        if (!reads[i+2]) return 0;
        *out=reads[i+3];return 1;
    }
    return 0;
}
static u8* load_with_cb(const char* path,u32* max_bytes,int* stage,ShaderInfo* si) {
    char sidecar[2200];snprintf(sidecar,sizeof sidecar,"%s.cbr",path);
    FILE* f=fopen(sidecar,"rb");
    if (!f) return NULL;
    nreads=fread(reads,4,sizeof reads/sizeof *reads,f);
    int more=fgetc(f);fclose(f);
    if (nreads%4 || more!=EOF) return NULL;
    snx_le32_array(reads,nreads);
    g_shader_cb_read=replay_read;
    return shader_corpus_load(path,max_bytes,stage,si);
}
#define shader_corpus_load load_with_cb
#include "C:/Users/brett/Documents/Codex/work/nvn-src/tools/snx_shaderc.c"
