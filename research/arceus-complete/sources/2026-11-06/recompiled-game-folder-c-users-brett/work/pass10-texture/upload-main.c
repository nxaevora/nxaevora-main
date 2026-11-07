/* Private harness glue. Production texture.c upload functions above are unmodified. */
static u64 fixture_reads, fixture_bytes;
void vk_guest_read(u64 gva, u64 size, u8* host, VkBuffer buf, u64 at) {
    (void)buf; (void)at;
    if (gva < 0x10000000ull || gva-0x10000000ull+size > fixture_size) fatal("fixture read out of bounds\n");
    memcpy(host, fixture_data+gva-0x10000000ull, size); fixture_reads++; fixture_bytes+=size;
}
u32 vk_surf_gen(void) { static u32 generation; return ++generation; }
void vk_surf_relist(Surf* s) { (void)s; }
void vk_surf_use(VkCommandBuffer cb, Surf* s, VkImageLayout layout, VkPipelineStageFlags2 stage, VkAccessFlags2 access) {
    vk_barrier(cb,s->img,VK_IMAGE_ASPECT_COLOR_BIT,
        s->layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        s->layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT,
        s->layout,stage,access,layout);
    s->layout=layout;
}
static u64 file_u64(size_t at) { return (u64)snx_ld_le32(fixture_data+at)|((u64)snx_ld_le32(fixture_data+at+4)<<32); }
int main(void) {
    const char* path="C:/Users/brett/Documents/Codex/work/arceus-dump/romfs/bin/field/model/pack/objects_area00_const05_01_nrm.bntx";
    FILE* file=fopen(path,"rb"); if (!file) fatal("fixture open failed\n");
    fseek(file,0,SEEK_END); fixture_size=ftell(file); rewind(file); fixture_data=malloc(fixture_size);
    if (fread(fixture_data,1,fixture_size,file)!=fixture_size) fatal("fixture read failed\n"); fclose(file);
    u64 brti=file_u64(file_u64(0x28)), table=file_u64(brti+0x70);
    u32 w=snx_ld_le32(fixture_data+brti+0x24), h=snx_ld_le32(fixture_data+brti+0x28);
    u32 levels=snx_ld_le16(fixture_data+brti+0x16), bh=snx_ld_le32(fixture_data+brti+0x34)&7;
    u32 size=snx_ld_le32(fixture_data+brti+0x50), format=snx_ld_le32(fixture_data+brti+0x1c);
    if (memcmp(fixture_data,"BNTX",4)||memcmp(fixture_data+brti,"BRTI",4)||w!=1024||h!=1024||levels!=11||bh!=4||format!=0x1e01)
        fatal("fixture differs from validated metadata\n");
    init();
    VkFmt f={.vk=VK_FORMAT_R16G16_UNORM,.bpp=16,.bw=4,.bh=4,.conv=VKC_BC5};
    Surf t={.gva=0x10000000ull+file_u64(table),.w=w,.h=h,.sw=w,.sh=h,.layers=1,.levels=levels,.depth3d=1,
        .fkey=0x24928,.f=f,.bl=1,.bh=bh,.bd=0,.size=size,.type=VK_IMAGE_VIEW_TYPE_2D,.layout=VK_IMAGE_LAYOUT_UNDEFINED};
    t.layer_size=bl_layer_size(w,h,1,&f,bh,0,levels);
    VkImageCreateInfo ii={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=f.vk,
        .extent={w,h,1},.mipLevels=levels,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
    if (!vk_image_create(&ii,&t.img,&t.mem)) fatal("image creation failed\n");
    if (!tex_upload(&t)||!t.upload_valid) fatal("production tex_upload failed\n");
    vk_surf_use(vk_cmd(),&t,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_READ_BIT);
    u64 total=0; static const unsigned fixture_bh[11]={4,4,3,2,1,0,0,0,0,0,0};
    for (u32 level=0;level<levels;level++) {
        u32 lw=w>>level,lh=h>>level;if(!lw)lw=1;if(!lh)lh=1;
        u32 nx=(lw+3)/4,ny=(lh+3)/4;u64 off=file_u64(table+level*8);
        u8* blocks=malloc((size_t)nx*ny*16);u8* expected=malloc((size_t)lw*lh*4);
        for(u32 y=0;y<ny;y++) for(u32 x=0;x<nx;x++) {
            u64 from=off+ref_bl(x*16,y,0,nx*16,ny,fixture_bh[level],0);
            if(from+16>fixture_size)fatal("expected block out of bounds\n");
            memcpy(blocks+((size_t)y*nx+x)*16,fixture_data+from,16);
        }
        bcn_decode_image(blocks,lw,lh,BCN_BC5,expected);
        VkBuffer rb;u8* actual=vk_readback_buffer((u64)lw*lh*4,&rb);
        VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1},.imageExtent={lw,lh,1}};
        rec_CmdCopyImageToBuffer(vk_cmd(),t.img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,rb,1,&copy);
        vk_finish();vk_readback_ready();
        u32 dif=0;for(u32 i=0;i<lw*lh*4;i++)if(actual[i]!=expected[i]) {if(dif++<8)fprintf(stderr,"level%u byte%u GPU%u CPU%u\n",level,i,actual[i],expected[i]);}
        if(dif)fatal("FAIL production tex_upload level%u %ux%u: %u bytes differ\n",level,lw,lh,dif);
        printf("PASS actual texture.c BC5 upload level%u %ux%u: %u bytes exact, BNTX offset0x%llx bh%u\n",level,lw,lh,lw*lh*4,(unsigned long long)off,fixture_bh[level]);
        total+=(u64)lw*lh*4;free(blocks);free(expected);
    }
    printf("PASS all11 mips: %llu decoded bytes exact; %llu fixture reads / %llu bytes, all bounded; image was uploaded through unchanged production texture.c functions.\n",(unsigned long long)total,(unsigned long long)fixture_reads,(unsigned long long)fixture_bytes);
    if(errors)fatal("%u reported Vulkan errors\n",errors);
    vk_image_destroy(t.img,t.mem);free(fixture_data);return 0;
}
