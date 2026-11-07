from pathlib import Path
p=Path('work/v2/rr-ngx.inc');s=p.read_text()
a=s.index('bool vk_rr_evaluate(');b=s.index('    NgxResourceVK input=',a)
creation=s[a:b].replace('bool vk_rr_evaluate(VkCommandBuffer cb,const VkFsr2Frame *f,RrGuides *guides)', 'bool vk_rr_create(VkCommandBuffer cb,const VkFsr2Frame *f)').replace('    bool reset=f->reset;\n','').replace('        reset=true;\n','')
s=s[:a]+creation+'''    return true;
}

bool vk_rr_evaluate(VkCommandBuffer cb,const VkFsr2Frame *f,RrGuides *guides) {
    bool reset=f->reset || !rr_feature;
    if(!vk_rr_create(cb,f))return false;
    NgxParam *p=rr_params;
'''+s[b:]
s=s.replace('    p_setp(p,"Color",&input);','''    NgxResourceVK before={0};
    if(guides->beforeView)
        before=res_image(guides->beforeView,guides->beforeImage,f->color_format,f->w,f->h,VK_IMAGE_ASPECT_COLOR_BIT,false);
    p_setp(p,"DLSSD.ColorBeforeTransparency",guides->beforeView?&before:NULL);
    p_setp(p,"Color",&input);''')
p.write_text(s)
p=Path('work/v2/rr-frame.h');s=p.read_text().replace('    float worldToView','    VkImage beforeImage;\n    VkImageView beforeView;\n    float worldToView').replace('bool vk_rr_available(void);','bool vk_rr_available(void);\nbool vk_rr_create(VkCommandBuffer cb,const VkFsr2Frame *frame);');p.write_text(s)
p=Path('work/v2/render.c');s=p.read_text()
s=s.replace('static void taa_after(', 'static void upscale_scene(Surf *col,Surf *z,bool opaqueStage);\nstatic void taa_after(')
s=s.replace('cap_depth=z->gva;cap_valid=1;','cap_depth=z->gva;cap_valid=1;\n upscale_scene(col,z,true);')
s=s.replace('.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};',
            '.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};')
a=s.index('static void upscale_before(');b=s.index(' if(!col||!z',a)
start=s[a:b]
gate=start[start.index(' if(disabled||'):].replace('if(disabled||','if(')
wrapper='''static void upscale_before(VkSh *fs,TexRes *tex,Surf *target){
 up_override=0;up_address=0;
'''+gate+''' if(col && z)upscale_scene(col,z,false);
}
'''
s=s[:a]+'''#include "opaque-scene.inc"
static void upscale_scene(Surf *col,Surf *z,bool opaqueStage){
 if(upscale_disabled)return;
 if(opaqueStage && (!tready || tlast+1!=g_vk_present || previous_color!=cap_color || tzaddr!=cap_depth))return;
'''+s[b:]
s=s.replace(' rr_capture_previous();\n float *cur=cap_matrix;', ''' rr_capture_previous();
 if(!opaqueStage && opaque.frame==g_vk_present && opaque.color==col->img && opaque.depth==z->img){
     opaque_resolve(col,z);
     return;
 }
 if(opaqueStage && !opaque_image_ready(col->sw,col->sh))return;
 float *cur=cap_matrix;''')
s=s.replace('!timages(target->sw,target->sh)','!timages(3840,2160)')
s=s.replace(' bool dl=vk_dlaa_wanted();',' bool dl=vk_dlaa_wanted();')
s=s.replace(' VkImageLayout vendor_layout=dl?rw:ro;', ' if(opaqueStage && !traced){vk_render_forget_state();return;}\n VkImageLayout vendor_layout=dl?rw:ro;')
a=s.index(' static bool previousRR;');b=s.index('\n\nstatic void reflection_record',a)
s=s[:a]+''' SceneResolve input={.f=f,.flow=p,.albedo=albedoView,.materialView=flow_set?material_view:VK_NULL_HANDLE,
     .materialAddress=material?material->gva:0,.dl=dl,.traced=traced,.guides=traced && rr.writing,
     .hasFlow=flow_set!=VK_NULL_HANDLE,.materialHistory=material_history};
 if(opaqueStage){
     if(input.guides && !vk_rr_create(cb,&input.f))input.guides=false;
     if(!input.guides)scene_filter(cb,&input);
     opaque_write(cb,col,z,&input);
     return;
 }
 rr.guides.beforeImage=VK_NULL_HANDLE;rr.guides.beforeView=VK_NULL_HANDLE;
 scene_finish(cb,col,z,&input);
}
'''+wrapper+s[b:]
p.write_text(s)
