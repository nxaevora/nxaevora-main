from pathlib import Path
import json, shlex, subprocess, shutil
root=Path.cwd();w=root/'work';out=w/'world'
shared=Path.home()/'Documents/Codex/work'
fix=shared.parent/'2026-11-06/fix'
ups=fix/'work/ups'
build=shared/'nvn-build'
s=(w/'pt/render.c').read_text()
s=s.replace('    o->topology = topology;', '''    int capture_positions=g_vk.has_xfb && !xfb_on && !tes_on && !gs_on && topology==4;
    if(capture_positions){
        memset(&xfb,0,sizeof xfb);
        xfb.count[0]=4;xfb.stride[0]=16;
        for(u32 j=0;j<4;j++)xfb.layout[0][j]=(u8)(28+j);
    }
    o->topology = topology;''').replace('xfb_on && !tes_on && !gs_on ? &xfb : NULL','(xfb_on || capture_positions) && !tes_on && !gs_on ? &xfb : NULL')
s=s.replace('#include "contact.h"','#include "contact.h"\n#include "capture.h"')
mark='    pred_begin(cb, ch->render_pred);\n    if (indexed) {'
assert s.count(mark)==1
s=s.replace(mark,'''    u64 captureCount=(u64)(indexed?icount:count)*instances;
    int capturing=!xfb_on && !tcs && !gs && !tes && topology==4 && zt && zt->sw==1920 && zt->sh==1080 &&
        n>=2 && captureCount<=1500000 && g_vk.has_xfb && world_capture_begin(cb,(u32)captureCount);
'''+mark)
mark='    pred_end(cb, ch->render_pred);\n    if (xcount)'
assert s.count(mark)==1
s=s.replace(mark,'    pred_end(cb, ch->render_pred);\n    if(capturing)rec_CmdEndTransformFeedbackEXT(cb,0,0,NULL,NULL);\n    if (xcount)')
(out/'render.c').write_text(s)
db=json.loads((build/'compile_commands.json').read_text())
entry=next(e for e in db if e['file'].endswith('/nx/gpu/vk/render.c'))
args=[v.strip('"') for v in shlex.split(entry['command'],posix=False)]
args[args.index('-o')+1]=str(out/'render.obj');args[-1]=str(out/'render.c')
subprocess.run(args+['-I'+str(w/'pt'),'-I'+str(ups)],cwd=build,check=True)
village=shared/'arceus-runs/offline-village-20261106-221228'
args=json.loads((fix/'work/rim-candidate/link-arguments.json').read_text())
args[args.index(str(fix/'work/reflection/surface.c.obj'))]=str(ups/'surface.obj')
a=args.index(str(fix/'work/rim-candidate/render.c.obj'))
args[a:a+1]=[str(out/'render.obj'),str(w/'query.obj'),str(w/'pt/device.obj')]+[str(ups/f) for f in ['common.obj','ngx.c.obj','fsr.cpp.obj']]
args[1:1]=[str(village/'mapped/mem.obj'),str(village/'wide/writeback.obj')]
args[args.index('-o')+1]=str(out/'ArceusTest.exe')
subprocess.run(args,cwd=build,check=True)
shutil.copy2(ups.parents[1]/'outputs/upscale-runtime/nvngx_dlss.dll',out)
