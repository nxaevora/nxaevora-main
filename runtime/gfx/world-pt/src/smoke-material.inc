static u64 smoke_texture_key(const Surf *s,u32 slot){
    static struct {VkImage image;u64 address,write,hash,key;u32 generation;} cache[2];
    if(!s || s->is_rt || s->w!=256 || s->h!=256 || s->size<2048 || !s->upload_valid)return 0;
    if(cache[slot].image==s->img && cache[slot].address==s->gva && cache[slot].write==s->wseq &&
       cache[slot].hash==s->hash && cache[slot].generation==s->wgen)return cache[slot].key;
    if(vk_buf_pending(s->gva,2048) || vk_compute_pending(s->gva,2048))return 0;
    const u8 *p=gmem_host(s->gva,2048);if(!p)return 0;
    u64 key=14695981039346656037ull;
    for(u32 j=0;j<2048;j++)key=(key^p[j])*1099511628211ull;
    cache[slot].image=s->img;cache[slot].address=s->gva;cache[slot].write=s->wseq;
    cache[slot].hash=s->hash;cache[slot].generation=s->wgen;cache[slot].key=key;
    return key;
}
static bool smoke_material(const VkSh *shader,const TexRes *tex){
    if(!tex)return false;
    const Surf *images[2]={0};
    for(int i=0;i<shader->si.ntex && i<SH_MAXTEX;i++){
        if(!tex[i].have)continue;
        if(shader->si.tex[i].cbuf_off==8)images[0]=tex[i].t;
        if(shader->si.tex[i].cbuf_off==10)images[1]=tex[i].t;
    }
    return smoke_texture_key(images[0],0)==0xf6c26469be628d5cull && smoke_texture_key(images[1],1)==0x375673c3af063351ull;
}
