from pathlib import Path
base=Path('work/v2')
p=base/'opaque-scene.inc';s=p.read_text().replace('    VkFsr2Frame f;','    VkFsr2Frame f;\n    VkRect2D rect;').replace('static bool upscale_disabled,previousRR;','static bool upscale_disabled,previousRR;\nstatic VkRect2D previousRect;').replace('    previous_color=cap_color;','    previousRect=input->rect;\n    previous_color=cap_color;');p.write_text(s)
p=base/'render.c';s=p.read_text().replace(' static VkRect2D previousRect;\n','').replace(' previousRect=activeRect;\n','').replace('SceneResolve input={.f=f,','SceneResolve input={.f=f,.rect=activeRect,');p.write_text(s)
p=base/'contact.h';s=p.read_text().replace('    bool auditPrimary;','    bool auditPrimary;\n    VkRect2D viewport;').replace('        float sx=world.rect.extent','        rr.viewport=world.rect;\n        float sx=world.rect.extent');p.write_text(s)
p=base/'rr-render.inc';s=p.read_text().replace('        bool ok=fwrite(tprev,sizeof(float),16,out)==16 &&','''        // The guest inverse matrix maps active viewport clip coordinates to world.
        // Convert from the full allocated depth image that is written to the dump.
        float inverse[16];
        float sx=(float)rr.viewport.extent.width/w,sy=(float)rr.viewport.extent.height/h;
        float tx=2.f*rr.viewport.offset.x/w+sx-1.f,ty=1.f-sy-2.f*rr.viewport.offset.y/h;
        memcpy(inverse,tprev,sizeof inverse);
        for(u32 i=0;i<4;i++){
            inverse[i]/=sx;inverse[4+i]/=sy;inverse[12+i]-=tx*inverse[i]+ty*inverse[4+i];
        }
        bool ok=fwrite(inverse,sizeof(float),16,out)==16 &&''')
p.write_text(s)
