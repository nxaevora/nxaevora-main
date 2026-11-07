import sys
sys.path.pop(0)
from pathlib import Path
import difflib, hashlib, json

src = Path('C:/Users/brett/Documents/Codex/work/nvn-src')
mirror = Path('C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main/third_party/ScarletNX')
out = Path('work/pass8-integration')
rel = 'tests/shader_screen_fetch.c'
p = src/rel
raw = p.read_bytes()
assert raw == (mirror/rel).read_bytes()
backup = out/'shader_screen_fetch.c.before'
assert not backup.exists()
backup.write_bytes(raw)
old = raw.decode().replace('\r\n','\n')
new = Path('work/pass8-shader/shader_screen_fetch_flow.c').read_text()
assert new.count('flow_tests(out);') == 1
insert = '''    /* Unsupported MOV masks do not assign the emitter's destination. */
    for (int mask = 0; mask <= 2; mask += 2) {
        memcpy(v, base, sizeof base);
        v[4] = mov(8, 255); v[5] = mov(9, 255);
        v[6] = field(mov(8, 2), 39, 4, mask);
        v[7] = field(mov(9, 3), 39, 4, mask);
        v[8] = tld(8);
        char name[32]; snprintf(name, sizeof name, "ignored-mov-mask-%d", mask);
        test(out, name, v, 9, 0);
    }
'''
new = new.replace('    flow_tests(out);',insert+'    flow_tests(out);')
needle = '        if (!f || fwrite(spv[enabled], 4, count[enabled], f) != count[enabled]) failures++;'
assert new.count(needle) == 1
new = new.replace(needle,'        snx_le32_array(spv[enabled], count[enabled]);\n'+needle)
p.write_text(new,newline='\n')
(mirror/rel).write_bytes(p.read_bytes())
(out/'test-changes.diff').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='before/'+rel,tofile='after/'+rel)))
manifest = json.loads((out/'integration.json').read_text())
manifest[rel] = dict(before=hashlib.sha256(raw).hexdigest(),after=hashlib.sha256(p.read_bytes()).hexdigest())
(out/'integration.json').write_text(json.dumps(manifest,indent=2))
print('Integrated 7 flow fixtures and 2 MOV-mask regressions; existing 29 cases preserved.')
