"""Check a completed F3 *_reflections.tsv, including the matching lighting bindings."""
import csv,json,sys
from pathlib import Path
p=Path(sys.argv[1]);text=p.read_text();rows=list(csv.DictReader((l for l in text.splitlines() if not l.startswith('#')),delimiter='\t'))
issues=[];by_address={}
for r in rows:
 f=Path(r['file']);n=int(r['bytes']);w=int(r['image_w']);h=int(r['image_h']);bw=int(r['block_width']);bh=int(r['block_height']);layers=int(r['layers'])
 expected=((w+bw-1)//bw)*((h+bh-1)//bh)*int(r['bpp'])*layers
 if r['status']!='ok' or not f.is_file() or f.stat().st_size!=n or n!=expected:issues.append('Bad payload: '+str(f))
 if layers%6:issues.append('Cube layer count not divisible by six: '+r['gva'])
 by_address.setdefault(r['gva'],set()).add(int(r['mip']))
for r in rows:
 if by_address[r['gva']]!=set(range(int(r['levels']))):issues.append('Incomplete mip chain: '+r['gva'])
lighting=p.with_name(p.name.replace('_reflections.tsv','_lighting.tsv'))
if not lighting.is_file():issues.append('Missing lighting manifest')
else:
 lines=lighting.read_text().splitlines()
 if not any(l.startswith('draw\t') for l in lines):issues.append('No targeted lighting draw captured')
 for l in lines:
  a=l.split('\t')
  if a[0]=='texture' and a[4] in ('34','36') and a[6] not in by_address:issues.append('Bound cube not captured: '+a[6])
  if a[0]=='cb' and (a[6]!='1' or a[8]!='1'):issues.append('Constant buffer omitted/unwritten: draw '+a[1]+' cb '+a[2])
if not rows:issues.append('No reflection textures captured')
report={'manifest':str(p),'mip_payloads':len(rows),'cube_addresses':len(by_address),'issues':sorted(set(issues)),'capture_headers':[l for l in text.splitlines() if l.startswith('#')]}
print(json.dumps(report,indent=2));sys.exit(bool(issues))
