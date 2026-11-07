from pathlib import Path
import json,re
files=list((Path.home()/'.codex/sessions/2026/11/07').glob('*01a1b57c*.jsonl'))
calls={}
for line in files[0].open(encoding='utf8'):
    try:r=json.loads(line)['payload']
    except Exception:continue
    if not isinstance(r,dict):continue
    if r.get('type') in ['custom_tool_call','function_call']:
        calls[r.get('call_id')]=str(r.get('input',r.get('arguments','')))
    if 'output' in r:
        pat=re.search(r'["\x27]session_id["\x27]\s*:\s*(\d+)',str(r['output']))
        if not pat:continue
        c=calls.get(r.get('call_id'),'')
        if any(k in c for k in ['forward-depth','placed-structures','brush-witness','survival-original']):
            print(pat[1],','.join(k for k in ['forward-depth','placed-structures','brush-witness','survival-original'] if k in c), c[-195:].replace(chr(10),' ')[:195])

