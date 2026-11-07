from pathlib import Path
p=Path(__file__).resolve().parent
s=(p/'integration.inc').read_text()
s=s.replace('xy[0]*=1.5f;xy[1]*=1.5f;','')
s=s.replace('(w*2+1)/3','1920').replace('(h*2+1)/3','1080')
a=s.index('static void taa_after');b=s.index(' VkComponentMapping id=',a)
s=s[:a]+'''static u32 cap_frame;static u64 cap_color,cap_depth;static float cap_matrix[16];static int cap_valid;
static void taa_after(GpuChannel *ch,VkSh *fs,TexRes *tex,Surf *col){
 if(!fs||!col||(fs->hash!=0x89619027ba43e5d3ull&&fs->hash!=0xe02c1a109a77fac4ull)||col->f.vk!=VK_FORMAT_B10G11R11_UFLOAT_PACK32||col->layers!=1||ch->render_pred)return;
 cap_valid=0;
 Surf *z=NULL;for(int i=0;i<fs->si.ntex&&i<SH_MAXTEX;i++)if(fs->si.tex[i].cbuf_off==0x32&&tex[i].have)z=tex[i].t;
 if(!z||z->sw!=1920||z->sh!=1080||col->sw!=1920||col->sh!=1080||z->f.vk!=VK_FORMAT_D24_UNORM_S8_UINT)return;
 u64 addr=ch->cbind[4][4];VkBuffer b;u64 off;if(!addr||cb_range(ch,4,fs,4)<256||vk_buf_pending(addr,256)||vk_compute_gpu_src(addr,256,&b,&off))return;
 const void *m=gmem_ptr(addr+192,64);if(!m)return;memcpy(cap_matrix,m,64);for(int i=0;i<16;i++)if(!isfinite(cap_matrix[i]))return;
 cap_frame=g_vk_present;cap_color=col->gva;cap_depth=z->gva;cap_valid=1;
}
static void upscale_before(VkSh *fs,TexRes *tex,Surf *target){
 static int disabled;up_override=0;up_address=0;
 if(disabled||!cap_valid||cap_frame!=g_vk_present||!fs||!target||target->sw!=2560||target->sh!=1440||
   (fs->hash!=0x63b3848c1a6e23e0ull && fs->hash!=0x8a160a27d90af72dull))return;
 Surf *col=NULL,*z=NULL;
 for(int i=0;i<fs->si.ntex&&i<SH_MAXTEX;i++)if(tex[i].have&&tex[i].t){
  if(tex[i].t->gva==cap_color)col=tex[i].t;
  if(tex[i].t->gva==cap_depth)z=tex[i].t;
 }
 if(!col||!z||col->sw!=1920||col->sh!=1080||z->sw!=1920||z->sh!=1080)return;
 if(tready && tlast==g_vk_present){up_override=th[1][1].view;up_address=cap_color;return;}
 float *cur=cap_matrix;
'''+s[b:]
s=s.replace('timages(col->sw,col->sh)','timages(target->sw,target->sh)').replace('u32 w=(tw*2+1)/3,h=(thh*2+1)/3;','u32 w=col->sw,h=col->sh;').replace('-pc.j[0]*w/tw','-pc.j[0]').replace('-pc.j[1]*h/thh','-pc.j[1]')
a=s.index(' tuse(cb,dst,VK_IMAGE_LAYOUT_TRANSFER_SRC');b=s.index(' LOG_LIMIT(32',a)
s=s[:a]+''' tuse(cb,dst,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);up_override=dst->view;up_address=cap_color;
'''+s[b:]
(p/'split.inc').write_text(s)
sf=(p.parent/'reflection/surface.c').read_text().replace('    if (is_rt && native_shadow_shape','''    /* Arceus 1.1.1 docked display chain confirmed in independent captures. Only these
       full-size, one-layer LDR surfaces get the 4/3 display scale. Unknowns are native. */
    if (!(is_rt && w==1920 && h==1080 && layers==1 &&
        ((gva==0x54d320000ull && fkey==0xd6) ||
        ((gva==0x553550000ull || gva==0x553dc0000ull) && fkey==0xd5)))) {s->sw=w;s->sh=h;}
    if (is_rt && native_shadow_shape''')
sf=sf.replace('float S = g_vk_scale;','float S = (float)st->sw/st->w, D=(float)dt->sw/dt->w;').replace('(dx + dw) * S','(dx + dw) * D').replace('(dy + dh) * S','(dy + dh) * D').replace('dx * S);','dx * D);').replace('dy * S);','dy * D);')
(p/'surface.c').write_text(sf)
