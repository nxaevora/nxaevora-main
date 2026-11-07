import sys
sys.path.pop(0)
from pathlib import Path
import hashlib, json

root=Path.cwd()
src=Path('C:/Users/brett/Documents/Codex/work/nvn-src')
mirror=Path('C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main/third_party/ScarletNX')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=json.loads((root/'work/pass9-integration/source-provenance.json').read_text())
assert sha(src/'src/nx/gpu/vk/render.c')==sha(mirror/'src/nx/gpu/vk/render.c')==p['render_after']
assert sha(src/'src/nx/gpu/shader/spirv_translate.c')==p['shader_source_unchanged']
assert sha(src/'src/nx/runtime/arceus_clock.c')==p['timing_source_unchanged']
baseline=sha(root/'work/ArceusEngineCandidate-runtime.exe')
installed=sha(Path('C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/ArceusTest.exe'))
assert baseline==installed==p['baseline_candidate']
runtime=sha(root/'work/ArceusMaterialDiagnostic-runtime.exe')
assert runtime==sha(Path('C:/Users/brett/Documents/Codex/work/nvn-build/scarletnx.exe'))
helper=(root/'work/pass9-integration/controller-material-launcher.c').read_text()
for needle in ['ArceusMaterialDiagnostic-runtime.exe','SNX_MATERIAL_TRACE",L"1','--nvn_native=0',
               '--native_bc=1','--resolution=1.3333333333333333','--gamepad_background=1','--seconds=0','WaitForSingleObject(game.hProcess,INFINITE)']:
    assert needle in helper,needle
comparison=(root/'work/pass9-integration/controller-bc-comparison-launcher.c').read_text()
assert '--native_bc=0' in comparison and '--native_bc=1' not in comparison
p.update(diagnostic_runtime=runtime,diagnostic_launcher=sha(root/'outputs/Arceus-Material-Diagnostic.exe'),
         bc_comparison_launcher=sha(root/'outputs/Arceus-BC-Texture-Comparison.exe'),
         diagnostic_native_bc=True,comparison_native_bc=False,bc_comparison_launched=False,
         baseline_candidate_unchanged=baseline,installed_runtime_unchanged=installed,
         game_launched=False,game_assets_changed=False,visual_fix_claimed=False,
         trace=dict(enabled_by='SNX_MATERIAL_TRACE=1',maximum_configurations=512,
                    scope='Existing CPU resolved-input metadata; no texture-content capture or rendering state change.'),
         source_review='work/pass9-render/material-trace-review.txt',
         runtime_build='work/pass9-integration/runtime-build.txt',
         evidence=['work/pass9-render/findings.txt','work/pass9-material/findings.txt','work/pass9-texture/'],
         latest_completed_game_run='timing-20261106-170438-469')
(root/'outputs/arceus-material-diagnostic-provenance.json').write_text(json.dumps(p,indent=2))
print(json.dumps({k:p[k] for k in ['diagnostic_runtime','diagnostic_launcher','baseline_candidate_unchanged','installed_runtime_unchanged','game_launched']},indent=2))
