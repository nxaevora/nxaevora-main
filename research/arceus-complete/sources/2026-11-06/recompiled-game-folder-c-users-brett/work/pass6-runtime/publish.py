import sys
sys.path.pop(0)
from pathlib import Path
import hashlib,json,difflib
root=Path.cwd();out=root/'outputs';private=root/'work'
base=Path(r'C:/Users/brett/Documents/Codex/work')
source=base/'nvn-src/src/nx/runtime/arceus_clock.c'
mirror=Path(r'C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main/third_party/ScarletNX/src/nx/runtime/arceus_clock.c')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(source)==sha(mirror)
for folder,dest in [('pass6-ai','arceus-ai-turn-research-pass6.txt'),('pass6-events','arceus-dialogue-research-pass6.txt'),('pass6-physics','arceus-physics-research-pass6.txt')]:
 text=(private/folder/'findings.txt').read_text()
 if folder=='pass6-physics':
  text=text.replace(' - Main caller return 2AF2AB8: world 145172000, mode=0, dt=1/30, 4 substeps, multi simulation, wrapper cadence near 120 calls/s.\n - Caller return 2CD701C: twelve sampled world addresses, variable input roughly 0.00795..0.02916 s, 2 substeps, single simulation.',
  ' - Completed capture: main caller return 2AF2AB8, world145172000,69 sampled steps, dt=1/30,4 substeps,multi simulation. The separate rigid-body wrapper probe observed mode0.\n - Caller return2CD701C:15 sampled world addresses,234 sampled steps, variable input roughly0.00795..0.02916s,2 substeps,single simulation.\n - Whole-run wrapper-entry cadence varied; it is not a count of this world\'s completed solves. Earlier live-snapshot counts are superseded by outputs/arceus-controller-test-pass6.json.')
 (out/dest).write_text(text)
files={'runtime_source':source,'runtime_mirror':mirror,'candidate_next_launch':private/'ArceusEngineCandidate-runtime.exe',
 'candidate_tested_and_preserved':private/'ArceusEngineCandidate-before-pass6-runtime.exe',
 'installed_tested_game_runtime':base/'arceus-games/ArceusTest/ArceusTest.exe',
 'regular_runtime_unchanged':private/'ArceusClockExperiment-runtime.exe','desktop_launcher':out/'Arceus-Controller-Test.exe',
 'log_summary':out/'summarize-arceus-research.py'}
record={'date':'2026-11-06','gameplay_changes_this_pass':'None. Launcher/controller support improved; next candidate adds one sampled read-only postsolve serial.',
 'files':{name:{'path':str(p),'sha256':sha(p)} for name,p in files.items()},
 'test_capture':str(base/'arceus-runs/timing-20261106-163108-125'),
 'counts':{'AI':9843,'message':1752,'resolution':1307,'total':12902,'failures':0,'compiled_entries':49},
 'fixtures':{name:json.loads((private/path).read_text()) for name,path in
 [('AI','pass6-ai/fixture-provenance.json'),('message','pass6-events/fixture-provenance.json'),('resolution','pass6-resolution/resolution-fixtures.json')]},
 'limits':'Original guest snippets/full selected methods with declared stubs; host-controlled proposed AI schedules; no new gameplay patch tested. Private extracted code/assets stay local.'}
(out/'arceus-research-provenance-pass6.json').write_text(json.dumps(record,indent=2)+'\n')
before=(private/'pass6-runtime/arceus_clock-before-pass6.c').read_text()
diff=''.join(difflib.unified_diff(before.splitlines(True),source.read_text().splitlines(True),fromfile='before/arceus_clock.c',tofile='after/arceus_clock.c'))
(private/'pass6-runtime/source-change.diff').write_text(diff)
notes=out/'controller-test-launch-notes.txt';s=notes.read_text()
s=s.replace('The candidate build is unchanged from pass4; pass5 findings were\nresearch and did not add new gameplay corrections.',
 'The completed163108 test used the pass4 candidate. Pass5/pass6 research added\nno gameplay corrections. The candidate staged AFTER that test now includes\none sampled read-only physics postsolve serial; it built successfully but has\nnot been tested in gameplay. Its camera/menu/timing/rendering behavior is unchanged.')
s+='\nCompleted test feedback: camera and menu felt good/better; visuals were good,\nbelow1440p, without the earlier distortion. Manual close confirmed exit0.\nKeep camera speed2.2 and menu-repeat fix enabled. The preserved tested runtime\nis work/ArceusEngineCandidate-before-pass6-runtime.exe. The regular package and\ninstalled game runtime were not replaced when staging the new diagnostic build.\n'
notes.write_text(s)
print('Published pass6 companion reports, provenance and updated launch notes.')
print('Next candidate SHA256:',sha(files['candidate_next_launch']))
print('Installed tested runtime SHA256:',sha(files['installed_tested_game_runtime']))
