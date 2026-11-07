import sys
sys.path.pop(0)
from pathlib import Path
import hashlib, json, shutil

root = Path.cwd()
src = Path('C:/Users/brett/Documents/Codex/work/nvn-src')
mirror = Path('C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main/third_party/ScarletNX')
build = Path('C:/Users/brett/Documents/Codex/work/nvn-build/scarletnx.exe')
game = Path('C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/ArceusTest.exe')
candidate = root/'work/ArceusEngineCandidate-runtime.exe'
before = root/'work/ArceusEngineCandidate-before-pass8-runtime.exe'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
old_hash = 'c88b5a620f1f627a2a07bc695eca49dc05dfddebecddc708d020a2c7d610a5df'
new_hash = 'fbe5a06b4b800143ae1cdf943975ea23b2279a2da23639239381b4719066da61'
assert sha(build) == new_hash
assert sha(before) == sha(candidate) == sha(game) == old_hash
manifest = json.loads((root/'work/pass8-integration/integration.json').read_text())
for rel in ['src/nx/gpu/shader/spirv_translate.c','src/nx/gpu/vk/pipeline.c','tests/shader_screen_fetch.c']:
    assert sha(src/rel) == sha(mirror/rel) == manifest[rel]['after']
assert sha(src/'src/nx/runtime/arceus_clock.c') == '3339e9e1fcc88932a4d1c1761360f2c6152e5bc6de3adc7b7b337290184c7f0d'
semantic = json.loads((root/'work/pass8-integration/semantics.json').read_text())
assert semantic['coordinate_cases'] == 27216 and semantic['failures'] == 0
assert '38 cases, 0 failures' in (root/'work/pass8-integration/recognizer-results.txt').read_text()
comparisons = json.loads((root/'work/pass8-final-corpus/comparisons.json').read_text())
assert not comparisons['provenance_changed_since_link']
for k in ['cached-off_vs_off-current-original','shipped-off_vs_off-shipped-original']:
    assert comparisons['comparisons'][k]['changed_count'] == 0
for k in ['cached-on_vs_current-original','shipped-on_vs_shipped-original']:
    assert comparisons['comparisons'][k]['changed_count'] == 1
shutil.copy2(build,candidate)
assert sha(candidate) == new_hash and sha(game) == old_hash
manifest.update(runtime_candidate=new_hash, runtime_installed=old_hash,
                candidate_staged=True, game_launched=False, game_assets_modified=False,
                gameplay_clock_source=sha(src/'src/nx/runtime/arceus_clock.c'),
                semantics=semantic, translated_shader_cache_revision='direct_flow=2',
                next_launcher=str(root/'outputs/Arceus-1440p-Controller-Test.exe'))
(root/'outputs/arceus-1440p-grid-fix-provenance.json').write_text(json.dumps(manifest,indent=2))
print('Staged verified runtime for the existing controller launcher. Installed game executable unchanged. No game launch.')
print('SHA256 '+sha(candidate))
