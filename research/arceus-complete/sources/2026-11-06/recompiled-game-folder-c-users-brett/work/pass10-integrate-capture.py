import sys
sys.path.pop(0)
from pathlib import Path
import hashlib, difflib

src = Path(r'C:/Users/brett/Documents/Codex/work/nvn-src')
mirror = Path(r'C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main/third_party/ScarletNX')
out = Path('work/pass10-integration')
helper = Path('work/pass10-snapshot/surface-capture.inc').read_bytes()
assert hashlib.sha256(helper).hexdigest() == '5e2fd3554d9f7cf061c5439b0624849ea9eec1d40da6c009513810a059b37092'
changed = {}
for name in ('surface.c', 'renderer.c', 'vk.h'):
    p = src/'src/nx/gpu/vk'/name
    assert p.read_bytes() == (out/(name+'.before')).read_bytes()
    assert (mirror/'src/nx/gpu/vk'/name).read_bytes() == p.read_bytes()
    changed[name] = p.read_text()

changed['surface.c'] += '\n' + helper.decode().replace('\r\n', '\n')
anchor = 'void vk_surf_touch(Surf* s);'
assert changed['vk.h'].count(anchor) == 1
changed['vk.h'] = changed['vk.h'].replace(anchor, anchor + '\n/* Optional local raw attachments, under vk_lock after a completed game present. */\nvoid vk_surf_diagnostic_capture(const char* prefix);')

s = changed['renderer.c']
anchor = 'static u32 g_last_transform;'
assert s.count(anchor) == 1
s = s.replace(anchor, anchor+'\n'+Path('work/pass10-renderer-capture.inc').read_text())
anchor = '    show(src, x1 - x0, y1 - y0, transform, 1);'
assert s.count(anchor) == 1
s = s.replace(anchor, anchor+'\n    material_snapshot_complete(src);')
anchor = '/* the last presented frame as PPM (path.ppm), and a hash of its pixels */'
assert s.count(anchor) == 1
s = s.replace(anchor, Path('work/pass10-renderer-complete.inc').read_text()+'\n'+anchor)
anchor = '                      : 1469598103934665603ull;\n    vk_unlock();\n    return hash;'
assert s.count(anchor) == 1
s = s.replace(anchor, '                      : 1469598103934665603ull;\n    material_snapshot_request(path);\n    vk_unlock();\n    return hash;')
changed['renderer.c'] = s

diff = []
for name, content in changed.items():
    before = (out/(name+'.before')).read_text()
    diff.extend(difflib.unified_diff(before.splitlines(True), content.splitlines(True), fromfile=name+'.before', tofile=name))
    for base in (src, mirror):
        (base/'src/nx/gpu/vk'/name).write_bytes(content.encode())
(out/'capture-source.diff').write_text(''.join(diff))
print('Integrated opt-in capture, preserving ordinary F3 screenshots and requiring completed game presents.')
