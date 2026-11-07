import sys
sys.path.pop(0)
from pathlib import Path
p=Path('work/pass8-render/trace.py').read_text().replace('work/pass8-render','work/pass9-render').replace('timing-20261106-164839-938','timing-20261106-170438-469')
Path('work/pass9-render/trace.py').write_text(p)
exec(compile(p,'work/pass9-render/trace.py','exec'))
