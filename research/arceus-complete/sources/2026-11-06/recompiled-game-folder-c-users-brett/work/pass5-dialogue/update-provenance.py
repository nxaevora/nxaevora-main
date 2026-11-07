import sys
sys.path.pop(0)
from pathlib import Path
import json
p=Path('work/pass5-dialogue/make-fixtures.py')
s=p.read_text().replace('Base text step: records byte delay/quota, writes stop=1 to out parameter, returns W0=4','Base text step: records byte delay/quota; configured bounded status/return sequence; defaults out-status1/W0=4')
s=s.replace('Full selected wrapper/tag/advance methods. Original full base text method executed only on non-parsing delay paths.','Full selected wrapper/tag/advance/drain methods. Original full base text method executed only on non-parsing delay paths. Inner-loop tests supply explicit sequential base-step statuses/returns.')
p.write_text(s)
meta=Path('work/pass5-dialogue/fixture-provenance.json')
d=json.loads(meta.read_text());d['host_stubs']['0x7000']='Base text step: records byte delay/quota; configured bounded status/return sequence; defaults out-status1/W0=4';d['scope']='Full selected wrapper/tag/advance/drain methods. Original full base text method executed only on non-parsing delay paths. Inner-loop tests supply explicit sequential base-step statuses/returns. Text glyph rendering, full message UI, input collector, live scheduling and asset/event callbacks not executed.';meta.write_text(json.dumps(d,indent=2)+'\n')
