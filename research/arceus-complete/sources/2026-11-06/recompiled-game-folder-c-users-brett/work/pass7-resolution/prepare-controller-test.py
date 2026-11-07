import sys
sys.path.pop(0)
from pathlib import Path
import difflib,json,hashlib
root=Path.cwd();folder=root/'work/pass7-resolution';folder.mkdir(exist_ok=True)
source=(root/'work/pass6-launch/controller-launcher.c').read_text()
changes=[
 ('#define UNICODE\n',''),
 ('L"Arceus controller test"','L"Arceus 1440p controller test"'),
 ('env(L"SNX_ARCEUS_FIXED_RESOLUTION",L"0")','env(L"SNX_ARCEUS_FIXED_RESOLUTION",L"1")'),
 ('env(L"SNX_ARCEUS_SCREEN_FETCH",L"0")','env(L"SNX_ARCEUS_SCREEN_FETCH",L"1")'),
 ('env(L"SNX_RESOLUTION_TRACE",L"0")','env(L"SNX_RESOLUTION_TRACE",L"1")'),
 ('120 FPS target; Vulkan; camera2.2; menu-repeat/blink/event-camera/timing fixes; manual close; controller background input enabled.',
  '1440p render-scale test; precise screen-fetch fix; 120 FPS target; Vulkan; camera2.2; menu-repeat/blink/event-camera/timing fixes; manual close; controller input enabled.'),
 ('\\"resolution_scale_override\\":0,\\"resolution_trace\\":false,\\"fixed_scene_resolution\\":false,\\"screen_fetch_fix\\":false',
  '\\"resolution_scale_override\\":1.3333333333333333,\\"docked\\":true,\\"resolution_trace\\":true,\\"fixed_scene_resolution\\":true,\\"screen_fetch_fix\\":true'),
 ('\\"fullscreen\\":false','\\"fullscreen\\":true'),
 ('--nvn_native=0 --fps_limit=game --fullscreen=0 --log=1 --seconds=0 --gamepad_background=1',
  '--nvn_native=0 --fps_limit=game --fullscreen=1 --log=1 --seconds=0 --gamepad_background=1 --docked=1 --resolution=1.3333333333333333')]
new=source
for old,replacement in changes:
 assert old in new,old
 new=new.replace(old,replacement)
(folder/'controller-1440p-launcher.c').write_text(new)
(folder/'launcher-profile.diff').write_text(''.join(difflib.unified_diff(source.splitlines(True),new.splitlines(True),fromfile='controller baseline',tofile='1440p controller test')))
for unchanged in ['SNX_ARCEUS_CAMERA_SPEED",L"2.2','SNX_ARCEUS_TARGET_FPS",L"120','SNX_ARCEUS_MENU_REPEAT_FIX",L"1','WaitForSingleObject(game.hProcess,INFINITE)','--seconds=0','SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS','Local\\\\ArceusControllerResearchLaunch']:
 assert unchanged in new,unchanged
print('Prepared separate1440p controller profile; accepted baseline launcher unchanged.')
