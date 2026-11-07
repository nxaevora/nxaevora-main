import sys
sys.path.pop(0)
from pathlib import Path
import difflib, hashlib, json, shutil

root=Path.cwd()
src=Path('C:/Users/brett/Documents/Codex/work/nvn-src')
mirror=Path('C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main/third_party/ScarletNX')
out=root/'work/pass9-integration'
out.mkdir(exist_ok=True)
rel='src/nx/gpu/vk/render.c'
p=src/rel
before=p.read_bytes()
assert before==(mirror/rel).read_bytes()
backup=out/'render.c.before'
assert not backup.exists()
backup.write_bytes(before)
old=before.decode().replace('\r\n','\n')
insert=(root/'work/pass9-material-trace.inc').read_text()
marker='/* Opt-in resolution diagnosis. Log each distinct viewport/target combination once;'
call='    resolution_trace(viewport[0], scissor[0], fs, fres, count, code + r[0x851]);'
assert old.count(marker)==1 and old.count(call)==1
new=old.replace(marker,insert+'\n'+marker).replace(call,call+'\n    material_trace(viewport[0], scissor[0], vs, fs, fres, count, scaled, r);')
p.write_text(new,newline='\n')
(mirror/rel).write_bytes(p.read_bytes())
(out/'render-trace.diff').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='before/'+rel,tofile='after/'+rel)))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest={'render_before':hashlib.sha256(before).hexdigest(),'render_after':sha(p),
          'shader_source_unchanged':sha(src/'src/nx/gpu/shader/spirv_translate.c'),
          'timing_source_unchanged':sha(src/'src/nx/runtime/arceus_clock.c'),
          'baseline_candidate':sha(root/'work/ArceusEngineCandidate-runtime.exe'),
          'installed_game':sha(Path('C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/ArceusTest.exe'))}
(out/'source-provenance.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps(manifest,indent=2))

helper=(root/'work/pass7-resolution/controller-1440p-launcher.c').read_text()
helper=helper.replace('Arceus 1440p controller test','Arceus material diagnostic')
helper=helper.replace('ArceusEngineCandidate-runtime.exe','ArceusMaterialDiagnostic-runtime.exe')
helper=helper.replace('env(L"SNX_RESOLUTION_TRACE",L"1");','env(L"SNX_RESOLUTION_TRACE",L"1");env(L"SNX_MATERIAL_TRACE",L"1");')
helper=helper.replace('1440p render-scale test; precise screen-fetch fix;','1440p material diagnostic; bounded texture/sampler metadata;')
helper=helper.replace('\\"resolution_trace\\":true','\\"resolution_trace\\":true,\\"material_trace\\":true')
for keep in ['SNX_ARCEUS_CAMERA_SPEED",L"2.2','SNX_ARCEUS_TARGET_FPS",L"120','WaitForSingleObject(game.hProcess,INFINITE)',
             '--nvn_native=0','--seconds=0','--gamepad_background=1','SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS','Local\\\\ArceusControllerResearchLaunch']:
    assert keep in helper,keep
(out/'controller-material-launcher.c').write_text(helper,newline='\n')
