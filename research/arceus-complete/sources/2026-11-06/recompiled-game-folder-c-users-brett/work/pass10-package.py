import sys
sys.path.pop(0)
from pathlib import Path
import hashlib, shutil, os, json, datetime

root = Path.cwd()
src = Path(r'C:/Users/brett/Documents/Codex/work/nvn-src')
mirror = Path(r'C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main/third_party/ScarletNX')
build = Path(r'C:/Users/brett/Documents/Codex/work/nvn-build/scarletnx.exe')
game = Path(r'C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/ArceusTest.exe')
run = Path(r'C:/Users/brett/Documents/Codex/work/arceus-runs/timing-20261106-172820-124')
out = root/'outputs'
work = root/'work/pass10-integration'

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def atomic_copy(source, target):
    temp = target.with_name(target.name+'.pass10-tmp')
    shutil.copyfile(source, temp)
    os.replace(temp, target)

new_hash = 'aaad4627683a6218594a7e0952e1e0ef2c748487c8b1f476271ea36da07c06b3'
assert sha(build) == new_hash
installed = sha(game)
assert installed == '550757a3d6451fb3c5e2007d99f9946eb4b2d979cb1aca2ddeff513425540a60'
candidate = root/'work/ArceusEngineCandidate-runtime.exe'
assert sha(candidate) == 'fbe5a06b4b800143ae1cdf943975ea23b2279a2da23639239381b4719066da61'
backup = root/'work/ArceusEngineCandidate-before-pass10-runtime.exe'
assert not backup.exists()
shutil.copyfile(candidate, backup)

files = ['src/nx/gpu/shader/spirv_translate.c', 'src/nx/gpu/vk/pipeline.c',
         'src/nx/gpu/vk/render.c', 'src/nx/gpu/vk/renderer.c', 'src/nx/gpu/vk/surface.c',
         'src/nx/gpu/vk/vk.h', 'tests/shader_screen_fetch.c', 'src/nx/runtime/arceus_clock.c']
sources = []
for relative in files:
    a, b = src/relative, mirror/relative
    assert a.read_bytes() == b.read_bytes(), relative
    sources.append(dict(path=str(a), mirror=str(b), sha256=sha(a)))
assert sha(src/'src/nx/runtime/arceus_clock.c') == '3339e9e1fcc88932a4d1c1761360f2c6152e5bc6de3adc7b7b337290184c7f0d'
atomic_copy(build, candidate)
atomic_copy(build, root/'work/ArceusSurfaceCapture-runtime.exe')
atomic_copy(work/'Arceus-Surface-Capture.exe', out/'Arceus-Surface-Capture.exe')
assert sha(game) == installed

checks = json.loads((root/'work/pass10-render/candidate-checks.json').read_text())
evidence = [root/'work/pass10-render/findings.txt', root/'work/pass10-render/snapshot-review.txt',
            root/'work/pass10-texture/findings.txt', root/'work/pass10-texture/capture-findings.txt',
            root/'work/pass10-material/findings.txt', root/'work/pass10-snapshot/integration-notes.txt',
            root/'work/pass10-integration/runtime-build.txt', root/'work/pass10-integration/recognizer-results.txt']
provenance = dict(time_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
    latest_completed_run=str(run), game_exit=0, user_result='BC comparison still shows bumps; changes during movement',
    staged_runtime_sha256=new_hash, installed_runtime_sha256=installed,
    staged_candidate=str(candidate), capture_runtime=str(root/'work/ArceusSurfaceCapture-runtime.exe'),
    preserved_candidate=str(backup), preserved_candidate_sha256=sha(backup),
    launcher=str(out/'Arceus-Surface-Capture.exe'), launcher_sha256=sha(out/'Arceus-Surface-Capture.exe'),
    shader_cache_token='direct_flow=3', sources=sources, shader_checks=checks,
    capture_helper_sha256=sha(root/'work/pass10-snapshot/surface-capture.inc'),
    evidence=[dict(path=str(p),sha256=sha(p)) for p in evidence],
    game_log_sha256=sha(run/'game-err.txt'), manual_game_launches=0,
    scope='Offline code/build/GPU fixtures only. No live game test of pass10; visual sufficiency pending. Files local.')
(out/'arceus-1440p-lighting-fix-provenance.json').write_text(json.dumps(provenance,indent=2))

report = '''1440p stone/wood artifacts: pass10, 2026-11-06

Current result
The BC texture comparison run timing-20261106-172820-124 closed normally with
game_exit=0. The user still sees the bumps, changing somewhat while moving.
The log confirms native_bc=0 really changed observed BC5 textures to the
software-decoded RG16 path. That decoder difference is not a sufficient cause.

Concrete correction now staged
A half-resolution lighting shader reads full-resolution packed normals using
2*screenPosition, but reads its half-resolution depth using screenPosition.
The existing resolution fix recognized only the latter. At 4/3 scaling the
normal fetch selected x=0,3,4,7,8,11,... instead of 1,3,5,7,9,11,... . The same
error affected Y. The new narrow recognizer follows the exact unmodified
FMUL.FTZ-by-2 instruction before integer conversion and preserves its mapping
in physical pixels. It keeps existing guards and unscaled/disabled behavior.
Cache token direct_flow=3 prevents reuse of old translated lighting shaders.

This is a demonstrated coordinate defect, not proof that it explains every
reported artifact. The observed shader contributes to a half-resolution light
buffer later sampled by main lighting. Its coverage at the pictured moments
has not been established. Five additional shipped light/probe shaders use the
same exact chain and receive the correction; they were not seen in this trace.

Validation
- Runtime build and production-linked 52 shader regression cases passed.
- 30,774 emitted-coordinate assertions passed; existing76 fixture outputs
  remain byte-identical. These evaluate coordinate expressions, not full GPU
  shaders or the game's visual result.
- 931 shipped fragments and334 captured shaders with constant-buffer replay
  translated with zero unsupported/failures. Flag-off output is byte-identical;
  flag-on changes only six shipped shaders and no captured-replay shaders.
- All245 captured BC5 bindings have complete declared mip chains and valid
  uploads. The known environment normal binding has linear/trilinear filters,
  no bias and no accidental sRGB. All observed raster depth bias is zero.
- A real local BC5 asset passed actual texture upload/deswizzle/conversion
  checks across11 mips. Finite sampler checks also passed documented device
  interpolation precision bounds. No sampler/upload defect reproduced.
- New capture helper passed12 real-GPU synthetic format/layer/depth fixtures,
  20 eligibility/overflow checks and the96-surface bound. Meaningful raw bytes,
  source image/stencil and tracked image state were preserved. No Vulkan
  validation layer or live-game capture was exercised.
- Independent review of helper and completed-present renderer integration
  found no blocking issue. All accepted gameplay timing source remains intact.

Next manual test
Open outputs/Arceus-Surface-Capture.exe. It uses the new staged runtime,
native BC enabled, Vulkan, the1440p profile, target120FPS, camera2.2, accepted
timing fixes and controller background input. It retains manual saving/closure.
The agent has not launched it and did not alter the installed closed-game EXE.

At affected stone, press F3 once. At the bridge, press F3 once again. Ordinary
screenshots are saved immediately. A separate capture is queued for the next
completed game present, with raw lighting/normal/depth buffers and a matching
*_buffers_N_final.ppm plus *_pair.tsv. A short stall is expected while copying
and writing locally. Save and close normally when done; Escape does not quit.

Capture limits and interpretation
Files stay under the game's local captures directory. At most4 extra captures
per run; at most96 surfaces,64MiB per image and384MiB raw data per capture.
The capture reads base mip/all array layers of recently used RTs and textures
copied from RTs, not every material texture or an entire replayable GPU frame.
Use each attachment's drawn_at/last_used metadata: some can be up to2 presents
old. The paired final is the guest final surface; it excludes runtime scaling,
letterboxing and overlay/UI. The ordinary earlier F3 screenshot is not its pair.
Raw buffers retain guest orientation; pair metadata records crop/transform.
D24 depth is low24 bits only; upper8 bits are undefined, not stencil.
Capture adds read copies and synchronization, not a new guest-writeback path;
ordinary completion may still land already pending game storage writes.

Build SHA256: {new_hash}
Installed tested EXE remains: {installed}
Preserved previous candidate: work/ArceusEngineCandidate-before-pass10-runtime.exe
Normal1440p launcher now points to the staged corrected candidate with capture
disabled. The dedicated Surface-Capture helper enables capture only for its run.
Visual confirmation is pending. All game assets, saves and fixtures stay local.
'''.format(new_hash=new_hash, installed=installed)
(out/'arceus-1440p-lighting-fix.txt').write_text(report)
shutil.copyfile(out/'arceus-material-investigation.txt',work/'material-report-before.txt')
(out/'arceus-material-investigation.txt').write_text(report)

agents = root/'AGENTS.md'
old = agents.read_text()
shutil.copyfile(agents,work/'AGENTS.md.before')
prefix = old[:old.index('Latest test')]
agents.write_text(prefix+'''Latest completed test is timing-20261106-172820-124, game_exit=0.
This was the BC comparison (--native_bc=0), confirmed by245 observed BC5
bindings using conversion22/RG16. User says bumps remain and change somewhat
while moving. Installed game EXE is still550757A3..., not the new candidate.

Pass10 found a separate half-resolution lighting coordinate defect: exact
FMUL.FTZ-by-2 screen coordinates were quantized/rescaled instead of reading
2*physical FragCoord. Correction is staged with cache token direct_flow=3.
52 regressions,30774 emitted-coordinate assertions,931 shipped fragments and
334 captured+CB replay shaders pass. OFF output identical; ON changes6 shipped
light/probe shaders. Visual sufficiency is unconfirmed; do not call the entire
stone/wood artifact fixed until the user's next test.

Staged work/ArceusEngineCandidate-runtime.exe and
work/ArceusSurfaceCapture-runtime.exe are AAAD4627... . PreviousFBE5A06B candidate
is preserved as work/ArceusEngineCandidate-before-pass10-runtime.exe; older
before-pass8 and before-pass6 backups remain. Source and mirror include pass10.
No game was launched by the agent this pass. Do not automatically launch.

Prepared outputs/Arceus-Surface-Capture.exe is the next manual controller test:
Vulkan, native_bc=1, docked4/3 scaling, fullscreen, target120FPS, camera2.2 and
accepted timing/menu/blink/effects fixes. Native helper retains manual closure.
It adds SNX_MATERIAL_SNAPSHOT=1: user's F3 saves the ordinary screenshot and
queues a separate raw-buffer capture at next completed game present. Max4
extra captures. Ask for stone/bridge F3 captures if artifacts persist; a brief
stall is expected. All files go to local game captures. Do not send input.

Use *_buffers_N_final.ppm plus *_pair.tsv as attachment pairing, not the earlier
ordinaryF3 image. *_attachments.tsv has format/dimensions/gva/age/raw filenames.
Some surfaces may be2 presents old; final excludes runtime postfx/letterbox/UI.
D24 raw highbyte is undefined: mask0x00ffffff. Helper passed12 GPU fixtures,
20 bound/eligibility checks and96countcap. No live-game capture tested yet.
work/decode-surface-capture.py MANIFEST [--normal-address HEX] uses stdlib only;
normal decode requires an address confirmed by the material trace/output slot.
Previous trace normal552210000 is a clue, not a hardcoded future identity.

Authoritative latest notes:
outputs/arceus-1440p-lighting-fix.txt
outputs/arceus-1440p-lighting-fix-provenance.json
outputs/arceus-material-investigation.txt
Old pass8 evidence remains valid but did not resolve the whole visual issue.
''')
print(json.dumps(dict(staged_sha256=new_hash, installed_unchanged=sha(game)==installed,
    launcher_sha256=sha(out/'Arceus-Surface-Capture.exe')),indent=2))
