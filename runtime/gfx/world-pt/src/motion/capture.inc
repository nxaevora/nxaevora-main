/* The F3 capture runs on the renderer thread. Keep the last submitted views'
   image handles only until the next temporal pass or resource resize. */
static struct {
 const char *backend,*mode;
 u32 frame,width,height;
 u64 material;
 VkImage motion,mask,current,previous;
 VkImageLayout motionLayout,maskLayout;
 FlowParams params;
} temporal_snapshot;

static void temporal_snapshot_set(const char *backend,const char *mode,u32 w,u32 h,u64 material,
                                  VkImage motion,VkImageLayout motionLayout,const FlowGpu *flow,const FlowParams *params){
 temporal_snapshot=(typeof(temporal_snapshot)){.backend=backend,.mode=mode,.frame=g_vk_present,.width=w,.height=h,
    .material=material,.motion=motion,.motionLayout=motionLayout,.params=*params};
 if(flow && flow->warm){
  temporal_snapshot.mask=flow->images[FlowMask].image;temporal_snapshot.maskLayout=flow->maskLayout;
  temporal_snapshot.current=flow->images[flow->historyIndex].image;
  if(params->historyValid)temporal_snapshot.previous=flow->images[1-flow->historyIndex].image;
 }
}
static void temporal_snapshot_capture(const char *prefix){
 char path[1200];int n=snprintf(path,sizeof path,"%s_temporal.tsv",prefix);
 if(n<0 || (size_t)n>=sizeof path)return;
 FILE *manifest=fopen(path,"wb");if(!manifest)return;
 if(!temporal_snapshot.backend || g_vk_present-temporal_snapshot.frame>1){
  fprintf(manifest,"status\tno-recent-temporal-dispatch\npresent\t%u\n",g_vk_present);
  fclose(manifest);return;
 }
 const typeof(temporal_snapshot) *s=&temporal_snapshot;
 fprintf(manifest,"version\t1\nbackend\t%s\nmode\t%s\npresent\t%u\nsource_frame\t%u\nsize\t%u\t%u\nmaterial_address\t%llx\n",
        s->backend,s->mode,g_vk_present,s->frame,s->width,s->height,(unsigned long long)s->material);
 fprintf(manifest,"history_valid\t%u\njitter_current_previous\t%.9g\t%.9g\t%.9g\t%.9g\nclip_to_previous",
        s->params.historyValid,s->params.jitter[0],s->params.jitter[1],s->params.previousJitter[0],s->params.previousJitter[1]);
 for(int i=0;i<16;i++)fprintf(manifest,"\t%.9g",s->params.clipToPrevious[i]);fputc('\n',manifest);
 const VkImage images[]={s->motion,s->mask,s->current,s->previous};
 const VkImageLayout layouts[]={s->motionLayout,s->maskLayout,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL};
 const char *names[]={"motion_rg16f","reactive_r8","current_features_rg32ui","previous_features_rg32ui"};
 const u32 stride[]={4,1,8,8};
 u64 bytes[4]={0},offsets[4]={0},size=0;
 for(int i=0;i<4;i++)if(images[i]){
  offsets[i]=(size+3)&~3ull;bytes[i]=(u64)s->width*s->height*stride[i];size=offsets[i]+bytes[i];
 }
 VkBuffer buffer;const u8 *mapped=size?vk_readback_buffer(size,&buffer):NULL;
 int ok=1;
 if(size && mapped){
  VkCommandBuffer cb=vk_cmd();vk_render_end();vk_rec_sync();
  for(int i=0;i<4;i++)if(images[i]){
   flow_image_layout(cb,images[i],layouts[i],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
   VkBufferImageCopy copy={.bufferOffset=offsets[i],.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={s->width,s->height,1}};
   vkCmdCopyImageToBuffer(cb,images[i],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
   flow_image_layout(cb,images[i],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,layouts[i]);
  }
  flow_memory(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
  vk_finish();vk_readback_ready();
  for(int i=0;i<4;i++)if(images[i]){
   n=snprintf(path,sizeof path,"%s_temporal_%s.bin",prefix,names[i]);
   FILE *out=n>0 && (size_t)n<sizeof path?fopen(path,"wb"):NULL;
   int complete=0;
   if(out){complete=fwrite(mapped+offsets[i],1,(size_t)bytes[i],out)==bytes[i];if(fclose(out))complete=0;}
   if(!complete)ok=0;
   fprintf(manifest,"image\t%s\t%llu\t%s\n",names[i],(unsigned long long)bytes[i],complete?"ok":"failed");
  }
 }else if(size)ok=0;
 fprintf(manifest,"status\t%s\n",ok?"ok":"readback-or-write-failed");fclose(manifest);
 LOG("arceus-motion: F3 %s frame=%u mode=%s bytes=%llu status=%d\n",s->backend,s->frame,s->mode,(unsigned long long)size,ok);
}
