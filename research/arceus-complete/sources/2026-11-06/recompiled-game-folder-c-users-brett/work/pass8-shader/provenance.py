import sys
sys.path.pop(0)
from pathlib import Path
import hashlib,json
paths=['work/pass8-shader/spirv_translate-original.c','work/pass8-shader/spirv_translate-fixed.c','work/pass8-shader/flow-tests.c','work/pass8-shader/shader_screen_fetch_flow.c','work/shipped-fragment-corpus/e9e7255e7594a006_4.snxs','work/shipped-fragment-corpus/e02c1a109a77fac4_4.snxs','work/shipped-fragment-corpus/a336734bf0d4c9c0_4.snxs']
rows=[dict(path=str(Path(x).resolve()),bytes=Path(x).stat().st_size,sha256=hashlib.sha256(Path(x).read_bytes()).hexdigest()) for x in paths]
Path('work/pass8-shader/fixture-provenance.json').write_text(json.dumps(dict(files=rows,notes=['Synthetic flow fixtures are authored in test source, not extracted game instruction sequences.','The original e9 fixture is the local shipped fragment SNXS; header24 bytes plus ShaderInfo2096 bytes; code5712 bytes; original maximum window131072 bytes.','Private source wrappers only expose static helper results, with no runtime patch; original/fixed differ in conditional KIL fall-through handling.','The 4536-case evaluator only evaluates emitted coordinate expressions and does not execute GPU image samples.']),indent=2))
print('report and provenance complete')
