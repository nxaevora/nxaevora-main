import sys
sys.path.pop(0)
from pathlib import Path
import runpy
summarize=runpy.run_path('outputs/summarize-arceus-research.py')['summarize']
p=Path('work/pass6-logs/world-fixture.log')
rows=[
'arceus-physics-step: count=300 ns=60 object=aaa caller=main+one step_seconds=0.0333333',
'arceus-physics-step: count=300 ns=60 object=bbb caller=main+two step_seconds=0.0083333',
'arceus-physics-simulation: count=300 ns=60 object=bbb simulation=222 tag=0 path=single alternate_world=0',
'arceus-physics-solver: count=300 ns=60 object=aaa input_seconds=0.0333333 stored_seconds=0.0333333 substep_seconds=0.0083333 substeps=4 alternate_world=0',
'arceus-physics-simulation: count=300 ns=60 object=aaa simulation=111 tag=1 path=multi alternate_world=0',
'arceus-physics-solver: count=300 ns=60 object=bbb input_seconds=0.0083333 stored_seconds=0.0083333 substep_seconds=0.0041666 substeps=2 alternate_world=0',
'arceus-physics-step: count=600 ns=90 object=aaa caller=main+one step_seconds=0.0333333',
'arceus-physics-solver: count=600 ns=90 object=aaa input_seconds=0.0333333 stored_seconds=nan substep_seconds=nan substeps=0 alternate_world=1',
'arceus-physics-step: count=900 ns=100 object=bbb caller=main+two step_seconds=0.01',
'arceus-physics-solver: count=900 ns=100 object=bbb input_seconds=0.01 stored_seconds=0.01 substep_seconds=0.005 substeps=2 alternate_world=0',
'arceus-physics-solver: count=900 ns=100 object=bbb input_seconds=0.01 stored_seconds=0.01 substep_seconds=0.005 substeps=2 alternate_world=0']
p.write_text('\n'.join(rows)+'\n')
r=summarize(p);a,b=r['physics_worlds']
assert a['world']=='aaa' and b['world']=='bbb'
assert a['normal_paths']==['multi'] and b['normal_paths']==['single']
assert a['normal_substeps']==[4] and b['normal_substeps']==[2]
assert a['normal_stored_seconds_range']==[.0333333,.0333333]
assert b['normal_stored_seconds_range']==[.0083333,.0083333]
assert a['alternate_solver_samples']==1 and a['matched_solver_samples']==2
assert b['ambiguous_solver_matches']==1 and b['matched_solver_samples']==1
assert a['matched_simulation_samples']==1 and b['matched_simulation_samples']==1
assert a['normal_simulation_objects']==['111'] and b['normal_simulation_objects']==['222']
p.write_text('\n'.join(reversed(rows))+'\n');assert summarize(p)['physics_worlds']==r['physics_worlds']
p.write_text('arceus-physics-step: count=1 object=aaa caller=main+one step_seconds=0.03\n')
r=summarize(p)['physics_worlds'][0];assert r['matched_solver_samples']==0 and r['matched_simulation_samples']==0
print('Physics-world grouping: interleaved/reversed rows, exact world identity, alternate/missing/ambiguous matches passed.')
serial_rows=[]
def sample(count,ns,serial,sim='111',alt=0):
 return [f'arceus-physics-step: count={count} ns={ns} object=aaa caller=main+one step_seconds=0.0333333',
 f'arceus-physics-solver: count={count} ns={ns} object=aaa input_seconds=0.0333333 stored_seconds=0.0333333 substep_seconds=0.0083333 substeps=4 alternate_world={alt} postsolve_serial={serial}',
 f'arceus-physics-simulation: count={count} ns={ns} object=aaa simulation={sim} tag=1 path=multi alternate_world={alt}']
serial_rows+=sample(300,60,4294967295)
serial_rows+=sample(600,90,0) # preserve wrap, do not calculate a negative rate
serial_rows+=sample(900,100,0,'222') # replacement simulation retained distinctly
serial_rows+=sample(1200,110,99,alt=1) # alternate world excluded
serial_rows+=sample(1500,120,100)
serial_rows.append(serial_rows[-2]) # duplicate solver row makes this join ambiguous
p.write_text('\n'.join(reversed(serial_rows))+'\n')
r=summarize(p)['physics_worlds'][0]
assert r['postsolve_observations']==[
 {'count':300,'ns':60,'simulation':'111','serial_u32':4294967295},
 {'count':600,'ns':90,'simulation':'111','serial_u32':0},
 {'count':900,'ns':100,'simulation':'222','serial_u32':0}]
assert r['ambiguous_solver_matches']==1
p.write_text('\n'.join(rows)+'\n')
assert all(not w['postsolve_observations'] for w in summarize(p)['physics_worlds'])
print('Postsolve observations: wrap, replacement, reordered logs, alternate/ambiguous exclusion and legacy absence passed.')
