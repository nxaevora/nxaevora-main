import sys
sys.path.pop(0)
from pathlib import Path
s=Path('work/disasm-functions.py').read_text().replace("Path('work/pass3')","Path('work/pass5-dialogue')").replace("(?<!#)0x", "(?<![#-])0x")
Path('work/pass5-dialogue/disasm.py').write_text(s)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import re
wanted=re.compile(r'(message|msg|text|input).*(wait|skip|speed|delay|frame|cool|key|button)|(wait|skip).*(message|msg|text|key)',re.I)
for f,refs in D['srefs'].items():
 hits=list(dict.fromkeys(s for _,_,s in refs if len(s)<150 and wanted.search(s)))
 if hits:print(f'{f:X}',hex(size.get(f,0)),hits)
