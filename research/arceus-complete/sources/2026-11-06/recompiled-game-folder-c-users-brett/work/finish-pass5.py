import sys
sys.path.pop(0)
from pathlib import Path
import re,json,hashlib
out=Path('outputs')
def read(p):
 raw=Path(p).read_bytes()
 return raw.decode('utf-16') if raw.startswith((b'\xff\xfe',b'\xfe\xff')) else raw.decode('utf-8-sig')
components=[
 ('dialogue','work/pass5-dialogue/findings.txt','arceus-dialogue-research-pass5.txt','work/pass5-dialogue/dialogue-checks.txt','work/pass5-dialogue/fixture-provenance.json'),
 ('AI scheduler','work/pass5-ai/scheduler-findings.txt','arceus-ai-scheduler-pass5.txt','work/pass5-ai/scheduler-checks.txt','work/pass5-ai/fixture-provenance.json'),
 ('event clocks','work/pass5-events/findings.txt','arceus-event-timers-pass5.txt','work/pass5-events/event-checks.txt','work/pass5-events/event-fixtures.json'),
 ('physics dispatch','work/pass5-physics/findings.txt','arceus-physics-scheduler-pass5.txt','work/pass5-physics/dispatcher-checks.txt','work/pass5-physics/dispatcher-fixture.json')]
lines=['ARCEUS ENGINE OFFLINE CHECKS, PASS 5','2026-11-06','Four isolated guest-instruction fixtures; no live game test.','']
total=0;compiled=0;provenance={}
for name,src,dst,check,meta in components:
 out.joinpath(dst).write_text(read(src),encoding='utf-8')
 log=read(check);m=re.search(r'checks=(\d+) failures=(\d+) compiled_functions=(\d+)',log)
 assert m,(name,log[-500:]);n,failed,entries=map(int,m.groups());assert failed==0
 total+=n;compiled+=entries
 lines.extend([name+':',log.strip(),''])
 provenance[name]=json.loads(read(meta))
assert total==85470 and compiled==24,(total,compiled)
lines.extend([f'Total: {total} checks; zero failures; {compiled} compiled guest-code entries.',
 'Entries include bounded instruction slices and dynamic branch entries, not only whole functions.',
 'Fixture responses for external code are synthetic; these are not live FPS measurements.',
 'Detailed extraction and stub boundaries: arceus-research-provenance-pass5.json.',
 'The dialogue and event suites were extended after independent cross-review; these are their final results.',
 'Runtime and packaged executables were not rebuilt or changed.'])
out.joinpath('arceus-engine-checks-pass5.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
out.joinpath('arceus-research-provenance-pass5.json').write_text(json.dumps(provenance,indent=2)+'\n',encoding='utf-8')
p=out/'arceus-engine-research-pass5.txt';s=p.read_text(encoding='utf-8-sig')
s=s.replace('Total85,399','Total85,470').replace('dialogue295 /9entries','dialogue344 /9entries').replace('event clocks728 /10entries','event clocks750 /10entries')
s=s.replace('temporarily disables control, fades out for30authored','temporarily disables control, fades out for30authored')
s=s.replace('can still advance the state because its return is ignored by this caller.','can still advance the state because its return is ignored by this caller.\nExit cleanup restores control/resets recovery clocks only when the first\nnavigator reference resolves; missing references return without those effects.')
s=s.replace('The normal defaults are restored afterwards.','The normal defaults are restored afterwards. Nonzero processing statuses can\n  also repeat the inner text loop without consuming that quota.')
s=s.replace('Individual reports and\nprovenance describe these limits.','Individual reports and\narceus-research-provenance-pass5.json describe these limits.')
p.write_text(s,encoding='utf-8')
triage=out/'arceus-integer-timer-triage.txt';s=triage.read_text(encoding='utf-8-sig')
marker='FIFTH-PASS FOLLOW-UP'
if marker not in s:
 s+='''\nFIFTH-PASS FOLLOW-UP\n\narceus-engine-research-pass5.txt adds verified field-message ownership and\nseparates embedded BE02 wait+188, reveal delay+9D/+9E, fresh-press advancement\nand menu repeat.2737ABC can deliberately drain text in one outer invocation;\nFBFBD0 also has an inner status-driven repeat bypassing the ordinary quota.\nDo not equate processor calls with rendered frames or throttle it wholesale.\nThe InputSkip references in107.../131... are expedition/Pokedex results;\nSingleInputEnableFrame belongs to tips configuration.\n\nThe AI callback now has a traced dispatch epoch/clock source and named category\nbuckets. Its frame accounting and decision cadence remain distinct. The four\npreviously unexplained event1/30scalar increments are catch-up/recovery clocks\nof RealTimeEventManager, not generic dialogue waits. All are offline findings;\nno new timing correction or runtime probe was installed in the fifth pass.\n'''
 triage.write_text(s,encoding='utf-8')
expected={
 'work/ArceusClockExperiment-runtime.exe':'69B7C027FAFC35C2AF8270CDCBF01B666E8931CF0C81E39F170193A19F64DDAF',
 'work/ArceusEngineCandidate-runtime.exe':'C2B2BE0B146D3C801A9E1E801243CA60D17965BDD363AF8DA5C746464B6753C1'}
for f,h in expected.items(): assert hashlib.sha256(Path(f).read_bytes()).hexdigest().upper()==h
for dst in ['arceus-engine-research-pass5.txt','arceus-engine-checks-pass5.txt',*[v[2] for v in components]]:
 text=out.joinpath(dst).read_text();assert text and '\ufffd' not in text
 print(dst,len(text),'characters')
print('Final artifact and unchanged-package checks passed; assertions',total)
