import sys
sys.path.pop(0)
from pathlib import Path
s=Path('work/trace-spv-fetch.py').read_text().replace('if depth>14:','if depth>7:').replace('for n,img,c in fetch:','for n,img,c in fetch[:3]:')
Path('work/pass9-render/first-fetches.py').write_text('import sys\nsys.path.pop(0)\n'+s)
