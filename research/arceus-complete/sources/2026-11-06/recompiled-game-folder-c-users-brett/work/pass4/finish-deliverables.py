import sys
sys.path.pop(0)
from pathlib import Path
import re
root=Path.cwd()
def read_text(path):
 raw=path.read_bytes()
 return raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8-sig')
reports={
 'work/pass4-physics/findings.txt':'outputs/arceus-physics-research-pass4.txt',
 'work/pass4-ai/ai-consumer-findings.txt':'outputs/arceus-ai-research-pass4.txt',
 'work/pass4-field/findings.txt':'outputs/arceus-field-timers-pass4.txt',
 'work/pass4-attention/findings.txt':'outputs/arceus-attention-timers-pass4.txt'}
for src,dst in reports.items():
 s=read_text(Path(src))
 if 'field' in dst:
  assert '255543E0' not in s and '25543E0' in s
 Path(dst).write_text(s,encoding='utf-8')
parts=['ARCEUS FOURTH-PASS OFFLINE INSTRUCTION CHECKS\nAll execution used synthetic guest memory. No interactive game was run.\n']
total=compiled=0
for label,folder,name in [
 ('Physics translation','pass4-physics','motion-checks.txt'),
 ('AI waits and turning','pass4-ai','ai-checks.txt'),
 ('Field timer branches','pass4-field','field-checks.txt'),
 ('Attention timer branches','pass4-attention','attention-checks.txt')]:
 s=read_text(Path('work')/folder/name)
 m=re.search(r'checks=(\d+) failures=(\d+) compiled_functions=(\d+)',s)
 assert m and int(m[2])==0
 total+=int(m[1]);compiled+=int(m[3])
 parts+=[label+'\n'+s]
assert (total,compiled)==(1813,15)
parts+=[f'TOTAL: {total} checks; zero failures; {compiled} compiled functions.\n',
 'The corresponding component reports describe extracted instruction boundaries,\nexternal stubs, continuation returns and limits. No live cadence is inferred.\n',
 'Runtime build: succeeded.\nLog summarizer: new/old-log fixtures passed, including unknown simulation paths.\nExisting instruction/function guards: 11 verified against local main.bin.\nSimulation vtable method identities: both verified against relocation data.\nRegular package: hash unchanged. Prior candidate retained; new build/package match.\nRuntime source/mirror: identical. Manual launchers: Vulkan, no time limit.\n']
Path('outputs/arceus-engine-checks-pass4.txt').write_text('\n'.join(parts),encoding='utf-8')
# Correct compressed prose from the earlier inventory update.
p=Path('outputs/arceus-integer-timer-triage.txt');s=p.read_text()
for a,b in [('Initializer254B254','Initializer main+254B254'),('value2','value 2'),('state2','state 2'),('at2588038','at main+2588038'),('float+D00','float +D00'),('Initializer1B5E5A0','Initializer main+1B5E5A0'),('to30','to 30'),('through2B90D40','through main+2B90D40'),('helper24D35B8','helper main+24D35B8')]:s=s.replace(a,b)
p.write_text(s)
print(f'Four companion reports and {total}-check result artifact saved to outputs.')
