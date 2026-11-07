from pathlib import Path
import re,subprocess,collections
root=Path(r'C:\Users\brett\Documents\Codex\work\arceus-runs\offline-village-20261106-221228'); summary={}
for label,binary in [('v2','wide'),('v3','mapped')]:
 d=root/('lookup-prof-'+label)
 series={}
 for p in sorted(d.glob('sample-*.txt'),key=lambda p:int(p.stem.split('-')[-1])):
  if int(p.stem.split('-')[-1])<3:continue
  t=p.read_text();m=re.search(r'(\d+) samples in (\d+) s',t)
  if m:series[int(m[2])]=(int(m[1]),{k:int(v) for v,k in re.findall(r'[\d.]+%\s+(\d+)  (.+)',t)})
 summary[label]=series
common=set(summary['v2'])&set(summary['v3'])
if not any(t>40 for t in common):print('waiting for comparable outdoor profiles');quit()
end=max(common);print('profile window',40,end)
for label,binary in [('v2','wide'),('v3','mapped')]:
 a=summary[label][40];z=summary[label][end]; keys=[k for k in set(a[1])|set(z[1]) if k.startswith('runtime +')]
 tool=r'C:\Users\brett\Documents\Codex\work\llvm-mingw-20261104-ucrt-x86_64\bin\llvm-addr2line.exe'
 syms=subprocess.check_output([tool,'-f','-e',str(root/binary/'ArceusTest.exe')]+[hex(0x140000000+int(k.split('+')[1],16)) for k in keys],text=True).splitlines()[::2]
 c=collections.Counter()
 for k,n in zip(keys,syms):
  if k in z[1]: c[n]+=max(0,z[1][k]-a[1].get(k,min(a[1].values())))
 print(label,'total thread samples',z[0]-a[0],'reported bins:',c['mem_mapped_run'],c['mapped_run_miss'],'top',c.most_common(3))

