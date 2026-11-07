static void forward_bindings(const VkSh *fs,const VkSh *vs,const TexRes *tex,Surf *target,u32 vertices,u32 instances){
    if(!fs || !target || !vs || !vertices || !(g_vk_present==600 || g_vk_present==1440 || g_vk_present==1960) ||
       target->gva!=cap_color || target->f.vk!=VK_FORMAT_B10G11R11_UFLOAT_PACK32 || target->sw!=1920 ||
       target->sh!=1080 || opaque.frame!=g_vk_present)return;
    LOG("forward-draw: frame=%u draw=%u fs=%016llx vs=%016llx vertices=%u instances=%u\n",
        g_vk_present,(u32)g_vk_drawn,(unsigned long long)fs->hash,(unsigned long long)vs->hash,vertices,instances);
    for(int i=0;i<fs->si.ntex && i<SH_MAXTEX;i++){
        const Surf *s=tex[i].t;
        if(!tex[i].have || !s || s->is_rt || s->size<2048 || vk_buf_pending(s->gva,2048) || vk_compute_pending(s->gva,2048))continue;
        const u8 *p=gmem_host(s->gva,2048);if(!p)continue;
        u64 key=14695981039346656037ull;
        for(u32 j=0;j<2048;j++)key=(key^p[j])*1099511628211ull;
        LOG("forward-image: frame=%u draw=%u fs=%016llx cb=%x image=%llx size=%ux%u fingerprint=%016llx\n",
            g_vk_present,(u32)g_vk_drawn,(unsigned long long)fs->hash,fs->si.tex[i].cbuf_off,
            (unsigned long long)s->gva,s->w,s->h,(unsigned long long)key);
    }
}
