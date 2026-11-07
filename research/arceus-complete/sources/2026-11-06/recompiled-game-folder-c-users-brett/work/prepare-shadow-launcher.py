import sys
sys.path.pop(0)
from pathlib import Path
import subprocess, hashlib
root=Path.cwd()
candidate=root/'work/native-shadow-candidate/ArceusNativeShadow-runtime.exe'
assert hashlib.sha256(candidate.read_bytes()).hexdigest()=='b32d40b03cee8adaf34da9150a657e97f25f9950e4ca25fbad7f641cf8caf019'
s=(root/'work/pass10-integration/controller-surface-capture-launcher.c').read_text()
s=s.replace('ArceusSurfaceCapture-runtime.exe',r'native-shadow-candidate\\ArceusNativeShadow-runtime.exe')
s=s.replace('Arceus surface capture','Arceus native shadow test')
s=s.replace('env(L"SNX_MATERIAL_SNAPSHOT",L"1");','env(L"SNX_MATERIAL_SNAPSHOT",L"1");env(L"SNX_ARCEUS_NATIVE_SHADOWS",L"1");')
s=s.replace('1440p surface capture; corrected half-resolution lighting;', '1440p native-shadow candidate; native 1024 shadow producers;')
s=s.replace('\\"material_snapshot\\":true,','\\"material_snapshot\\":true,\\"native_shadows\\":true,')
assert 'TerminateProcess' not in s
for token in ['--nvn_native=0','--seconds=0','--gamepad_background=1','--resolution=1.3333333333333333','L"120"','WaitForSingleObject(game.hProcess,INFINITE)']:
    assert token in s
p=root/'work/native-shadow-candidate/controller-shadow-launcher.c';p.write_text(s)
compiler=Path('C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe')
subprocess.run([str(compiler),'-O2','-municode','-mwindows',str(p),'-o',str(root/'outputs/Arceus-Shadow-Controller-Test.exe'),'-luser32'],check=True)
print('Shadow controller helper built; candidate hash verified; manual closure and existing test settings retained.')
