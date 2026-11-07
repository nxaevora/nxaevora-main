#include "rr-frame.h"

static NgxParam *rr_params;
static NgxHandle *rr_feature;
static bool rr_failed;
static bool rr_supported;

bool vk_rr_available(void) {
    static bool checked;
    if (!checked && N.available) {
        checked=true;
        NgxParam *caps=NULL;
        int value=0;
        rr_supported=NGX_OK(N.caps(&caps)) && caps &&
            p_geti(caps,"SuperSamplingDenoising.Available",&value) && value;
        LOG("arceus-rr: driver capability %s\n",rr_supported?"available":"unavailable");
    }
    return rr_supported && !rr_failed;
}

static bool rr_error(const char *operation,NgxResult result) {
    LOG("arceus-rr: %s failed (0x%08x), switching to standard denoising\n",operation,(unsigned)result);
    rr_failed=true;
    return false;
}

bool vk_rr_evaluate(VkCommandBuffer cb,const VkFsr2Frame *f,RrGuides *guides) {
    if (!vk_rr_available()) return false;
    if (!rr_params) {
        NgxResult result=N.alloc_params(&rr_params);
        if (!NGX_OK(result) || !rr_params) return rr_error("parameter allocation",result);
    }
    NgxParam *p=rr_params;
    bool reset=f->reset;
    if (!rr_feature) {
        p_setu(p,"CreationNodeMask",1); p_setu(p,"VisibilityNodeMask",1);
        p_setu(p,"Width",f->w); p_setu(p,"Height",f->h);
        p_setu(p,"OutWidth",f->ow); p_setu(p,"OutHeight",f->oh);
        p_seti(p,"PerfQualityValue",0);
        p_seti(p,"DLSS.Feature.Create.Flags",NGX_FLAG_MV_LOWRES|NGX_FLAG_DEPTH_INVERTED|1|64);
        p_seti(p,"DLSS.Denoise.Mode",1);
        p_setu(p,"DLSS.Roughness.Mode",1);
        p_setu(p,"DLSS.Use.HW.Depth",1);
        NgxResult result=N.create(cb,13,p,&rr_feature);
        if (!NGX_OK(result) || !rr_feature) return rr_error("feature creation",result);
        LOG("arceus-rr: Ray Reconstruction created %ux%u -> %ux%u\n",f->w,f->h,f->ow,f->oh);
        reset=true;
    }
    NgxResourceVK input=res_image(f->color_view,f->color,f->color_format,f->w,f->h,VK_IMAGE_ASPECT_COLOR_BIT,false);
    NgxResourceVK output=res_image(f->out_view,f->out,f->out_format,f->ow,f->oh,VK_IMAGE_ASPECT_COLOR_BIT,true);
    NgxResourceVK depth=res_image(f->depth_view,f->depth,f->depth_format,f->w,f->h,VK_IMAGE_ASPECT_COLOR_BIT,false);
    NgxResourceVK motion=res_image(f->mv_view,f->mv,VK_FORMAT_R16G16_SFLOAT,f->w,f->h,VK_IMAGE_ASPECT_COLOR_BIT,false);
    static const char *const keys[]={"DLSS.Input.DiffuseAlbedo","DLSS.Input.SpecularAlbedo","GBuffer.Normals","DLSSD.SpecularHitDistance"};
    NgxResourceVK resource[RR_GUIDE_COUNT];
    for(unsigned i=0;i<RR_GUIDE_COUNT;i++){
        VkFormat format=i==RR_HIT_DISTANCE?VK_FORMAT_R32_SFLOAT:VK_FORMAT_R16G16B16A16_SFLOAT;
        resource[i]=res_image(guides->view[i],guides->image[i],format,f->w,f->h,VK_IMAGE_ASPECT_COLOR_BIT,false);
        p_setp(p,keys[i],&resource[i]);
    }
    p_setp(p,"Color",&input); p_setp(p,"Output",&output);
    p_setp(p,"Depth",&depth); p_setp(p,"MotionVectors",&motion);
    p_setp(p,"WorldToViewMatrix",guides->worldToView); p_setp(p,"ViewToClipMatrix",guides->viewToClip);
    p_setf(p,"Jitter.Offset.X",f->jx); p_setf(p,"Jitter.Offset.Y",f->jy);
    p_setf(p,"MV.Scale.X",1); p_setf(p,"MV.Scale.Y",1);
    p_setf(p,"DLSS.Pre.Exposure",1); p_setf(p,"DLSS.Exposure.Scale",1);
    p_setu(p,"DLSS.Render.Subrect.Dimensions.Width",f->w);
    p_setu(p,"DLSS.Render.Subrect.Dimensions.Height",f->h);
    p_setf(p,"FrameTimeDeltaInMsec",f->dt_ms); p_seti(p,"Reset",reset);
    NgxResult result=N.evaluate(cb,rr_feature,p,NULL);
    if(!NGX_OK(result)) return rr_error("evaluation",result);
    return true;
}
