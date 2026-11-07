/* Diagnostic CPU snapshots only. Never flush or mutate guest state here.
   Pending GPU-written constants are explicitly omitted rather than mislabeled. */
#define REF_DRAW_MAX 8
void vk_reflection_bindings_capture(const char* prefix);
static struct {
    u64 hash, target;
    u32 present, mask, nt;
    struct { u64 address; u32 size, status; u8 data[65536]; } cb[18];
    struct { u64 address; u32 word, tic[8], tsc[8], format, levels; } tex[SH_MAXTEX];
} reflection_draw[REF_DRAW_MAX];
static u32 reflection_present = ~0u, reflection_count, reflection_dropped;
static int reflection_enabled(void) {
    static int on = -1;
    if (on < 0) { const char* e = getenv("SNX_REFLECTION_CAPTURE"); on = e && !strcmp(e,"1"); }
    return on;
}
static void reflection_record(GpuChannel* ch, VkSh* fs, const TexRes* tex) {
    if (!reflection_enabled() || !fs || !tex) return;
    if (reflection_present != g_vk_present) {
        reflection_present = g_vk_present; reflection_count = 0; reflection_dropped = 0;
    }
    /* Both known deferred lighting variants. Other shaders remain outside scope. */
    if (fs->hash != 0xe02c1a109a77fac4ull && fs->hash != 0x89619027ba43e5d3ull) return;
    if (reflection_count == REF_DRAW_MAX) { reflection_dropped++; return; }
    typeof(reflection_draw[0])* d = &reflection_draw[reflection_count++];
    d->hash = fs->hash; d->present = g_vk_present; d->mask = fs->si.cbuf_mask & 0x3ffff;
    d->target = g_rp.n && g_rp.rt[0] ? g_rp.rt[0]->gva : 0;
    for (u32 i = 0; i < 18; i++) {
        d->cb[i].status = 0; d->cb[i].size = 0; d->cb[i].address = ch->cbind[4][i];
        if (!(d->mask & (1u << i))) continue;
        u32 n = cb_range(ch,4,fs,i); d->cb[i].size = n;
        u64 a = d->cb[i].address;
        if (!a) { memset(d->cb[i].data,0,n); d->cb[i].status = 1; continue; }
        VkBuffer b; u64 off;
        if (vk_buf_pending(a,n) || vk_compute_gpu_src(a,n,&b,&off)) { d->cb[i].status = 2; continue; }
        const void* p = gmem_ptr(a,n);
        if (!p) { d->cb[i].status = 3; continue; }
        memcpy(d->cb[i].data,p,n); d->cb[i].status = 1;
    }
    d->nt = fs->si.ntex < SH_MAXTEX ? fs->si.ntex : SH_MAXTEX;
    for (u32 i = 0; i < d->nt; i++) {
        memset(&d->tex[i],0,sizeof d->tex[i]);
        d->tex[i].word = fs->si.tex[i].cbuf_off;
        if (!tex[i].have) continue;
        memcpy(d->tex[i].tic,tex[i].tic,sizeof d->tex[i].tic);
        memcpy(d->tex[i].tsc,tex[i].tsc,sizeof d->tex[i].tsc);
        if (tex[i].t) { d->tex[i].address=tex[i].t->gva; d->tex[i].format=tex[i].t->f.vk; d->tex[i].levels=tex[i].t->levels; }
    }
}
void vk_reflection_bindings_capture(const char* prefix) {
    if (!reflection_enabled() || !prefix || !*prefix || strpbrk(prefix,"\t\r\n")) return;
    char path[1100];
    int n=snprintf(path,sizeof path,"%s_lighting.tsv",prefix);
    if(n<0 || (size_t)n>=sizeof path) return;
    FILE* f=fopen(path,"wb"); if(!f) { LOG("reflection-bindings: cannot open manifest\n"); return; }
    u32 failures=0;
    fprintf(f,"# capture_present=%u recorded_present=%u draws=%u dropped=%u constants=CPU-at-draw-not-GPU-readback status1=valid-CPU status2=GPU-pending-omitted status3=unmapped\n",g_vk_present,reflection_present,reflection_count,reflection_dropped);
    for(u32 i=0;i<reflection_count;i++) {
        typeof(reflection_draw[0])* d=&reflection_draw[i];
        fprintf(f,"draw\t%u\tpresent\t%u\tfs\t%llx\ttarget\t%llx\n",i,d->present,(unsigned long long)d->hash,(unsigned long long)d->target);
        for(u32 j=0;j<18;j++) if(d->mask & (1u<<j)) {
            n=snprintf(path,sizeof path,"%s_light_%u_cb%u.bin",prefix,i,j);
            int ok=0;
            if(d->cb[j].status==1 && n>=0 && (size_t)n<sizeof path) {
                FILE* b=fopen(path,"wb");
                if(b) { ok=fwrite(d->cb[j].data,1,d->cb[j].size,b)==d->cb[j].size; if(fclose(b)) ok=0; }
                if(!ok) failures++;
            }
            fprintf(f,"cb\t%u\t%u\t%llx\t%u\tstatus\t%u\twritten\t%d\t%s\n",i,j,(unsigned long long)d->cb[j].address,d->cb[j].size,d->cb[j].status,ok,ok?path:"-");
        }
        for(u32 j=0;j<d->nt;j++) {
            fprintf(f,"texture\t%u\t%u\tword\t%x\taddress\t%llx\tformat\t%u\tlevels\t%u\ttic",i,j,d->tex[j].word,(unsigned long long)d->tex[j].address,d->tex[j].format,d->tex[j].levels);
            for(u32 k=0;k<8;k++) fprintf(f,"\t%08x",d->tex[j].tic[k]);
            fprintf(f,"\ttsc");for(u32 k=0;k<8;k++) fprintf(f,"\t%08x",d->tex[j].tsc[k]);fputc('\n',f);
        }
    }
    if(ferror(f)) failures++; if(fclose(f)) failures++;
    LOG("reflection-bindings: present=%u recorded=%u draws=%u dropped=%u failures=%u\n",g_vk_present,reflection_present,reflection_count,reflection_dropped,failures);
}
