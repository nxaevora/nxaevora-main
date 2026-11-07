/* Opt-in cosmetic workaround, not a proven emulation correction.
   Match the captured scene's two category1 color entries exactly. */
static int rim_candidate_enabled(void) {
    static int on = -1;
    if (on < 0) { const char* e = getenv("SNX_ARCEUS_NEUTRAL_RIM"); on = e && !strcmp(e,"1"); }
    return on;
}
static int rim_candidate_match(const void* data, u32 size, int stage, int bank, u64 hash) {
    static const u32 purple[4] = {0x3ee9aeb6,0x3e405416,0x3f3ce7b4,0x3ef0f0f1};
    static const u32 warm[4] = {0x3f800000,0x3f4a9282,0x3f54ad57,0x3f169697};
    if (!data || size < 0x160 || size > 65536 || stage != 4 || bank != 3 ||
        (hash != 0x89619027ba43e5d3ull && hash != 0xe02c1a109a77fac4ull)) return 0;
    return !memcmp((const u8*)data+0x150,purple,sizeof purple) &&
           !memcmp((const u8*)data+0xd0,warm,sizeof warm);
}
static void rim_candidate_apply(void* data) {
    float rgb[3]; memcpy(rgb,(u8*)data+0x150,sizeof rgb);
    float y = .2126f*rgb[0] + .7152f*rgb[1] + .0722f*rgb[2];
    rgb[0]=rgb[1]=rgb[2]=y;
    memcpy((u8*)data+0x150,rgb,sizeof rgb);
}
static void rim_candidate_buffer(GpuChannel* ch, int stage, int bank, VkSh* shader,
                                  u32 size, VkBuffer* buffer, u64* offset) {
    if (!rim_candidate_enabled() || stage!=4 || bank!=3 ||
        (shader->hash!=0x89619027ba43e5d3ull && shader->hash!=0xe02c1a109a77fac4ull)) return;
    u64 address=ch->cbind[stage][bank]; VkBuffer pending; u64 pending_offset;
    if (!address || vk_buf_pending(address,size) || vk_compute_gpu_src(address,size,&pending,&pending_offset)) {
        LOG_LIMIT(4,"arceus-neutral-rim: skipped non-CPU constant buffer\n"); return;
    }
    const void* src=gmem_ptr(address,size);
    if (!rim_candidate_match(src,size,stage,bank,shader->hash)) return;
    VkBuffer b; u64 o;
    void* dst=vk_stage(size,vk_stage_align(),&b,&o);
    if (!dst) { LOG_LIMIT(4,"arceus-neutral-rim: allocation failed; original lighting retained\n"); return; }
    memcpy(dst,src,size); rim_candidate_apply(dst);
    *buffer=b; *offset=o;
    LOG_LIMIT(8,"arceus-neutral-rim: applied category1 purple tint workaround fs=%llx; alpha and luminance preserved\n",(unsigned long long)shader->hash);
}
