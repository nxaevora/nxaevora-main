import sys
sys.path.pop(0)
from pathlib import Path
import json,struct
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
word=lambda p:struct.unpack_from('<I',b,p)[0]
look=[word(p) for p in range(0x19c2f80,0x19c65b8,4)]
face=[word(p) for p in range(0x24d17dc,0x24d199c,4)]
# At the first instruction of vigilance-end event setup, branch to the existing
# epilogue. This isolates countdown storage from asset/configuration effects.
face.append(0x14000000|(((0x24d183c-0x24d199c)//4)&0x3ffffff))
lines=['/* Local-only game instruction fixtures; do not upload. */']
for name,words in [('look',look),('face_prefix',face)]:
 lines.append(f'static const u32 {name}_code[]={{'+','.join(hex(w) for w in words)+'};')
Path('work/pass4-attention/attention-fixtures.h').write_text('\n'.join(lines)+'\n')
Path('work/pass4-attention/fixture-provenance.json').write_text(json.dumps({
 'look':{'begin':'19C2F80','end_exclusive':'19C65B8','altered_words':0,'external_mock':'virtual restore slot +100; validates arguments and returns'},
 'face_prefix':{'begin':'24D17DC','end_exclusive':'24D199C','boundary':'appended B from 24D199C to original epilogue24D183C','limitations':'No vigilance-end expression setup executed; object handles null'}},indent=2))
print('Attention instruction fixtures extracted locally.')
