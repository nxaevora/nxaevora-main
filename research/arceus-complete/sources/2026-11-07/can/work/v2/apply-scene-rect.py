from pathlib import Path
base=Path('work/v2')
def edit(file,old,new):
 p=base/file;s=p.read_text();assert s.count(old)==1,(file,old);p.write_text(s.replace(old,new))
edit('material.inc','VkImage albedo,depth;} captureTargets','VkImage albedo,depth;VkRect2D rect;} captureTargets')
edit('stream.inc','u64 albedoAddress;','u64 albedoAddress;\n    VkRect2D rect;')
edit('stream.inc','Surf *depth,GpuChannel','Surf *depth,VkViewport vp,GpuChannel')
edit('stream.inc','{albedo->img,depth->img};','''{albedo->img,depth->img,
        {{(i32)roundf(vp.x),(i32)roundf(fminf(vp.y,vp.y+vp.height))},
         {(u32)roundf(vp.width),(u32)roundf(fabsf(vp.height))}}};''')
edit('stream.inc','u32 sourceStart=0,selected=0;','''world.rect=(VkRect2D){{0,0},{1920,1080}};
    u32 sourceStart=0,selected=0,largest=0;''')
edit('stream.inc','            selected+=sourceEnd-sourceStart;','''            u32 n=sourceEnd-sourceStart;
            VkRect2D r=captureTargets[i].rect;
            if(n>largest && r.offset.x>=0 && r.offset.y>=0 && r.extent.width && r.extent.height &&
               r.extent.width<=1920u-(u32)r.offset.x && r.extent.height<=1080u-(u32)r.offset.y &&
               r.offset.x<1920 && r.offset.y<1080){world.rect=r;largest=n;}
            selected+=n;''')
edit('render.c','rt[1],zt,ch,fs,fres);','rt[1],zt,vp,ch,fs,fres);')
edit('terrain.inc','float grid[4];} LandInfo','float grid[4],viewport[4];} LandInfo')
edit('terrain.inc','sizeof(LandInfo)==144','sizeof(LandInfo)==160')
edit('contact.comp','uint hasShading;vec4 grid;','uint hasShading;vec4 grid;vec4 viewport;')
edit('contact.h','memcpy(land.gpu.jitter,jitter,sizeof land.gpu.jitter);','''memcpy(land.gpu.jitter,jitter,sizeof land.gpu.jitter);
    land.gpu.viewport[0]=(float)world.rect.offset.x;land.gpu.viewport[1]=(float)world.rect.offset.y;
    land.gpu.viewport[2]=(float)world.rect.extent.width;land.gpu.viewport[3]=(float)world.rect.extent.height;''')
edit('contact.h','rr.guides.viewToClip[0]=1.f/pc.camera[0]; rr.guides.viewToClip[5]=1.f/pc.camera[1];','''float sx=world.rect.extent.width/1920.f,sy=world.rect.extent.height/1080.f;
        rr.guides.viewToClip[0]=sx/pc.camera[0];rr.guides.viewToClip[5]=sy/pc.camera[1];
        rr.guides.viewToClip[8]=2.f*world.rect.offset.x/1920.f+sx-1.f;
        rr.guides.viewToClip[9]=1.f-sy-2.f*world.rect.offset.y/1080.f;''')
edit('contact.comp','+u.draws.land.jitter/vec2(size);','+u.draws.land.jitter/u.draws.land.viewport.zw;')
edit('contact.comp','ivec2 p=ivec2(uv*vec2(size));','ivec2 p=ivec2(u.draws.land.viewport.xy+uv*u.draws.land.viewport.zw);')
p=base/'contact.comp';s=p.read_text();assert s.count('.5-u.draws.land.jitter)/vec2(size)')==2
p.write_text(s.replace('.5-u.draws.land.jitter)/vec2(size)','.5-u.draws.land.jitter-u.draws.land.viewport.xy)/u.draws.land.viewport.zw'))
edit('particle-grid.glsl','(uv.xy*2.-1.)','(((uv.xy*vec2(1920,1080)-u.draws.land.viewport.xy)/u.draws.land.viewport.zw)*2.-1.)')
edit('render.c',' pc.valid=tready && tlast+1',''' static VkRect2D previousRect;
 // Motion and all temporal consumers use coordinates in the allocated image.
 // Conjugate the camera transform by the guest viewport to keep them in that space.
 VkRect2D activeRect=world.selected?world.rect:(VkRect2D){{0,0},{w,h}};
 pc.valid=!memcmp(&previousRect,&activeRect,sizeof activeRect) && tready && tlast+1''')
edit('render.c',' rec_CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tpipe);',''' if(pc.valid && (activeRect.extent.width!=w || activeRect.extent.height!=h)){
     float sx=(float)activeRect.extent.width/w,sy=(float)activeRect.extent.height/h;
     float tx=2.f*activeRect.offset.x/w+sx-1.f,ty=1.f-sy-2.f*activeRect.offset.y/h;
     for(u32 i=0;i<4;i++){
         pc.m[i]/=sx;pc.m[4+i]/=sy;pc.m[12+i]-=tx*pc.m[i]+ty*pc.m[4+i];
     }
     for(u32 j=0;j<16;j+=4){
         pc.m[j]=sx*pc.m[j]+tx*pc.m[j+3];pc.m[j+1]=sy*pc.m[j+1]+ty*pc.m[j+3];
     }
 }
 previousRect=activeRect;
 rec_CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tpipe);''')
