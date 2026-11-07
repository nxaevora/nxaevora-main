import sys
sys.path.pop(0)
from pathlib import Path

folder=Path('work/pass9-integration')
p=folder/'controller-material-launcher.c'
source=p.read_text()
assert source.count('--nvn_native=0')==1
source=source.replace('--nvn_native=0','--nvn_native=0 --native_bc=1')
source=source.replace('\\"material_trace\\":true','\\"material_trace\\":true,\\"native_bc\\":true')
p.write_text(source,newline='\n')
comparison=source.replace('Arceus material diagnostic','Arceus BC texture comparison')
comparison=comparison.replace('--native_bc=1','--native_bc=0')
comparison=comparison.replace('\\"native_bc\\":true','\\"native_bc\\":false')
comparison=comparison.replace('1440p material diagnostic; bounded texture/sampler metadata;',
                              '1440p BC texture comparison; alternate BC decoder and material metadata;')
for unchanged in ['ArceusMaterialDiagnostic-runtime.exe','SNX_MATERIAL_TRACE",L"1','--nvn_native=0',
                  '--gamepad_background=1','--seconds=0','--resolution=1.3333333333333333',
                  'WaitForSingleObject(game.hProcess,INFINITE)','SNX_ARCEUS_CAMERA_SPEED",L"2.2',
                  'SNX_ARCEUS_TARGET_FPS",L"120']:
    assert unchanged in source and unchanged in comparison,unchanged
(folder/'controller-bc-comparison-launcher.c').write_text(comparison,newline='\n')
print('Prepared explicit native-BC reference and alternate-BC comparison profiles; no game launched.')
