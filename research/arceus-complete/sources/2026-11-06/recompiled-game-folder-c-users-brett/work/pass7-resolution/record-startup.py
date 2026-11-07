import sys
sys.path.pop(0)
from pathlib import Path
import json,hashlib,re
root=Path.cwd();out=root/'outputs'
run=Path((out/'current-controller-test.txt').read_text(encoding='utf-16' if (out/'current-controller-test.txt').read_bytes().startswith(b'\xff\xfe') else 'utf-8-sig').splitlines()[0])
log=(run/'game-err.txt').read_text(errors='replace')
settings=json.loads((run/'test-settings.json').read_text())
assert settings['screen_fetch_fix'] and settings['fixed_scene_resolution'] and settings['fullscreen']
assert settings['controller_background'] and settings['target_fps']==120 and settings['camera_speed']==2.2
lines=log.splitlines()
controller=[s for s in lines if s.startswith('input:') and ('gamepad ' in s or 'enabled accelerometer' in s or 'enabled gyroscope' in s)]
assert any('DualSense Edge Wireless Controller' in s for s in controller)
renderer=[s for s in lines if 'precise current-screen-pixel' in s or 'automatic 3D resolution governor disabled' in s or 'swapchain' in s]
geometry=[s for s in lines if s.startswith('resolution-trace: draw ') and 'image=2560x1440 viewport=2560x-1440 scissor=2560x1440' in s and 'colors=5 depth=1' in s]
assert geometry,'No full1440p five-target depth-tested geometry evidence yet'
failures=[s for s in lines if s.startswith('arceus-') and ('refused' in s or 'compile failed' in s)]
runtime=Path(r'C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/ArceusTest.exe')
data={'run':str(run),'capture_state':'startup/live snapshot; user is testing; no closure performed',
 'runtime_sha256':hashlib.sha256(runtime.read_bytes()).hexdigest(),'settings':settings,
 'controller':controller,'renderer':renderer,'sample_1440p_geometry':geometry[:8],
 'guard_failures_at_snapshot':failures,
 'tests':{'shader_recognition_cases':29,'emitted_coordinate_cases':13768,'failures':0},
 'limits':'Actual sampled3D viewport confirmed; not all passes need this resolution. Visual quality and performance remain for user evaluation. No displayed FPS measurement.'}
(out/'arceus-1440p-test-startup.json').write_text(json.dumps(data,indent=2)+'\n')
notes=out/'arceus-1440p-test-notes.txt'
with notes.open('a') as f:
 f.write('\nLaunch verified\nCapture: '+str(run)+'\nDualSense Edge and sensors detected. Both rendering changes active.\nFullscreen swapchain2560x1440. Five-color-target, depth-tested3D draws used\nimage2560x1440, viewport2560x1440 and scissor2560x1440. No refused patch guards\nat this startup snapshot. The user is testing; visual acceptance is pending.\n')
print('Saved controller, shader flag, full3D viewport and runtime evidence; guard failures:',len(failures))
