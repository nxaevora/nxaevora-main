import sys
sys.path.pop(0)
from pathlib import Path
import csv,json,hashlib
root=Path.cwd(); out=root/'work/native-shadow-candidate'
captures=Path('C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/captures')
result=[]
for p in sorted(captures.glob('screenshot_20261106_175*_buffers_*_attachments.tsv')):
    rows=list(csv.DictReader(p.read_text().splitlines()[1:],delimiter='\t'))
    selected=[];scene=[]
    for r in rows:
        choose=r['is_rt']=='1' and r['guest_w']==r['guest_h']=='1024' and int(r['fkey'],16) in (0xf6,0x1000a)
        if choose:selected.append({k:r[k] for k in ['index','gva','fkey','image_w','image_h','layers']})
        if r['guest_w']=='1920' and r['guest_h']=='1080' and r['is_rt']=='1':
            assert not choose
            assert r['image_w']=='2560' and r['image_h']=='1440'
            scene.append(r['index'])
    assert len(selected)==4,(p.name,selected)
    assert {r['gva'] for r in selected}=={'54a8a0000','54aca0000','53aca0000','53ada0000'}
    result.append(dict(manifest=str(p),attachments=len(rows),selected=selected,unchanged_1440p_scene_indices=scene))
assert len(result)==4
# Original native-copy branch is still present; candidate changes only crop sizing here.
texture=(out/'texture.c').read_text()
assert 'if (s->sw == s->w && s->sh == s->h)' in texture
# Verify the source files remain precisely the compilation inputs.
source=Path('C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/gpu/vk')
for row in json.loads((out/'provenance.json').read_text()):
    assert hashlib.sha256((source/row['file']).read_bytes()).hexdigest()==row['base_sha256']
(out/'capture-scope.json').write_text(json.dumps(result,indent=2))
print(f'{sum(x["attachments"] for x in result)} captured attachments checked; exactly four shadow producer surfaces per capture selected; all full scene targets retain 2560x1440.')
