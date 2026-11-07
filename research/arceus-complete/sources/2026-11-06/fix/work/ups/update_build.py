from pathlib import Path
p=Path(__file__).resolve().parent;f=p/'build.py';s=f.read_text().replace("(p/'integration.inc').read_text()","(p/'split.inc').read_text()")
a=s.index("(p/'render.c').write_text(s)")
s=s[:a]+'''s=s.replace("static VkDescriptorSet stage_set_(GpuChannel* ch", "static VkImageView up_override;static u64 up_address;\\nstatic VkDescriptorSet stage_set_(GpuChannel* ch",1)
s=s.replace("    DrawImages images = {0};","    DrawImages images = {0};\\n    upscale_before(fs,fres,n?rt[0]:NULL);")
s=s.replace("        u32 b = SNX_VKB_TEX + i;","        if(stage==4 && up_override && t && t->gva==up_address && want==VK_IMAGE_VIEW_TYPE_2D){view=up_override;layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;if(scaled)*scaled|=1ull<<i;}\\n        u32 b = SNX_VKB_TEX + i;")
s=s.replace("zt->sw==tw && zt->sh==thh","zt->sw==1920 && zt->sh==1080").replace("viewport[0].width-tw","viewport[0].width-1920").replace("viewport[0].height)-thh","viewport[0].height)-1080")
a=s.index("    int selected = 0, other = 0;",s.index("static float target_raster_scale"));b=s.index("\\n}",a);s=s[:a]+"""    float scale=0;for(u32 i=0;i<=n;i++){Surf *t=i==n?zt:rt[i];if(!t)continue;
        float q=(float)t->sw/t->w;if(scale && fabsf(scale-q)>.001f){LOG_LIMIT(8,"arceus-upscaler: mixed-scale attachments\\\\n");return 0;}scale=q;}return scale?scale:1;"""+s[b:]
s=s.replace("native_shadow_shape(d->w, d->h, d->fkey) ? 1.0f : g_vk_scale","(float)d->sw/d->w")
cc('/src/nx/gpu/vk/surface.c','surface.c','surface.obj')
'''+s[a:]
s=s.replace("a=args.index(str(rim/'render.c.obj'))","args[args.index(str(p.parent/'reflection/surface.c.obj'))]=str(p/'surface.obj');a=args.index(str(rim/'render.c.obj'))")
f.write_text(s)
