from pathlib import Path
import csv,statistics as stats,io,contextlib,subprocess,hashlib
base=Path(__file__).resolve().parent
envsrc=base/'package-wide.py'
s=envsrc.read_text().replace('village-v2-runtime','village-v3-runtime').replace('Village-CPU-v2','Village-CPU-v3').replace('village-v2-test','village-v3-test').replace('village_writeback_v2','village_mapped_lookup_v3').replace("base/'wide/ArceusTest.exe'","base/'mapped/ArceusTest.exe'").replace("base/'wide/controller-village.c'","base/'mapped/controller-village.c'").replace('Village writeback optimization','Village mapped-memory optimization')
exec(compile(s,str(envsrc),'exec'))
test=base/'mapped/lookup-test.exe'
subprocess.run([str(test)],check=True)
shutil.copy2(base/'mapped/mem-mapped-run.patch',private/'mem-mapped-run.patch')
assert hashlib.sha256(runtime.read_bytes()).digest()==hashlib.sha256((base/'mapped/ArceusTest.exe').read_bytes()).digest()
lines=['Arceus village v3 compared with packaged v2 on the Ryzen 7 9800X3D.','', 'V3 separates cache hits from the locked lookup and keeps both range bounds in the same cache entry. V2 shader writeback is included.', '','Profiler off. Average FPS over successful presentation submissions. p95 is the 95th percentile interval between submissions in milliseconds. Same settings and private starting save for all eight runs; both run orders tested per scene.', '']
for scene,low,high in [('dialog',40,60),('street',70,85)]:
 for version in ('v2','v3'):
  fps=[]
  for rep in ['a','b']:
   d=base/f'lookup-{scene}-{version}-{rep}'
   with (d/'frames.csv').open() as f: ts=[int(r['begin_ns'])/1e9 for r in csv.DictReader(f) if r['result']=='0']
   win=[x for x in ts if low<=x<=high]; dt=[1000*(y-x) for x,y in zip(win,win[1:])]
   fps.append(1000/stats.mean(dt))
   lines.append(f'{d.name} [{low},{high}] seconds: {fps[-1]:.2f} FPS, p95 {stats.quantiles(dt,n=100)[94]:.2f} ms.')
  lines.append(f'{scene} {version} two-run mean: {stats.mean(fps):.2f} FPS.')
lines.extend(['','The candidate was faster in both dialogue pairs. Street pairs differed in direction. Final frames show the same dialogue and street landmarks; player/camera position differs slightly on the street.', '', '32 million hot lookups per direct test, six interleaved runs per build. This synthetic measurement uses a prewarmed mapping and tests the cached path.'])
with (base/'mapped/lookup-microbench.csv').open() as f: rows=list(csv.DictReader(f))
for v in ['v2','v3']:
 vals=[float(r['ns_per_cached_call']) for r in rows if r['build']==v];lines.append(f'{v} median {stats.median(vals):.3f} ns/call.')
lines+=['','Correctness fixture passed: adjacent ranges, holes, partial lengths, unmapped and end-of-space addresses, cache collisions, stale generations and lookup under the existing lock.','', 'Matched CPU sampling (only top 200 locations retained, includes waiting threads; listed counts conservatively assign newly appearing bins):']
capture=io.StringIO()
with contextlib.redirect_stdout(capture):exec((base/'profile-lookup.py').read_text(),{})
lines.append(capture.getvalue().strip())
lines.extend(['',f'Test build, fixtures and raw captures: {base}',f'Tested runtime SHA-256: {hashlib.sha256(runtime.read_bytes()).hexdigest()}'])
(private/'benchmark.txt').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines[-5:]))

