static void check_float_control(void) {
    Surf t={.w=1024,.h=1024,.sw=1024,.sh=1024,.levels=11,.layers=1,.depth3d=1,.type=VK_IMAGE_VIEW_TYPE_2D,
        .f={.vk=VK_FORMAT_R32G32_SFLOAT,.bpp=8,.bw=1,.bh=1},.layout=VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageCreateInfo ii={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=t.f.vk,
        .extent={1024,1024,1},.mipLevels=11,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
    if(!vk_image_create(&ii,&t.img,&t.mem))fatal("float control image failed\n");
    vk_surf_use(vk_cmd(),&t,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT);
    for(u32 l=0;l<11;l++) {
        u32 w=1024>>l;if(!w)w=1;u64 at;VkBuffer staged;
        float* dst=(float*)vk_stage((u64)w*w*8,vk_stage_align(),&staged,&at);
        for(u32 i=0;i<w*w*2;i++)dst[i]=cpu_mips[l][i]/65535.0f;
        VkBufferImageCopy copy={.bufferOffset=at,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,l,0,1},.imageExtent={w,w,1}};
        rec_CmdCopyBufferToImage(vk_cmd(),staged,t.img,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    }
    check_filter(&t);vk_image_destroy(t.img,t.mem);
}
