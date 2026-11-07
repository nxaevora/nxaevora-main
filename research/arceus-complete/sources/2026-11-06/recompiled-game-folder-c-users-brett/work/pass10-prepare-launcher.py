import sys
sys.path.pop(0)
from pathlib import Path

out = Path('work/pass10-integration')
out.mkdir(parents=True, exist_ok=True)
s = Path('work/pass9-integration/controller-material-launcher.c').read_text()
assert 'ArceusMaterialDiagnostic-runtime.exe' in s
s = s.replace('ArceusMaterialDiagnostic-runtime.exe', 'ArceusSurfaceCapture-runtime.exe')
s = s.replace('Arceus material diagnostic', 'Arceus surface capture')
s = s.replace('env(L"SNX_MATERIAL_TRACE",L"1");',
              'env(L"SNX_MATERIAL_TRACE",L"1");env(L"SNX_MATERIAL_SNAPSHOT",L"1");')
assert '1440p material diagnostic; bounded texture/sampler metadata;' in s
s = s.replace('1440p material diagnostic; bounded texture/sampler metadata;',
              '1440p surface capture; corrected half-resolution lighting; F3 saves lighting buffers;')
# The JSON embedded in the C string records the actual opt-in capture mode.
s = s.replace('\\"material_trace\\":true,',
              '\\"material_trace\\":true,\\"material_snapshot\\":true,\\"screen_fetch_2x\\":true,')
assert 'SNX_MATERIAL_SNAPSHOT' in s and '--native_bc=1' in s
assert 'WaitForSingleObject(game.hProcess,INFINITE)' in s
assert '--seconds=0' in s and '--nvn_native=0' in s
assert 'TerminateProcess' not in s
(out/'controller-surface-capture-launcher.c').write_text(s)
print(out/'controller-surface-capture-launcher.c')
