import sys
sys.path.pop(0)
from pathlib import Path
import hashlib, shutil

src = Path(r'C:/Users/brett/Documents/Codex/work/nvn-src')
mirror = Path(r'C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main/third_party/ScarletNX')
out = Path('work/pass10-integration')
target = src/'src/nx/gpu/shader/spirv_translate.c'
before = (out/'spirv_translate.c.before').read_bytes()
assert target.read_bytes() == before
assert (mirror/'src/nx/gpu/shader/spirv_translate.c').read_bytes() == before
new = Path('work/pass10-render/spirv_translate.c').read_bytes()
assert hashlib.sha256(new).hexdigest() == 'e306271885d8b8dea509f9b7082e4a17863779a9f0dbe668f3aecdedf1dc6b45'
# Preserve the production files' LF line endings.
new = new.replace(b'\r\n', b'\n')
assert hashlib.sha256(new).hexdigest() == 'ac376b4ef2db63023a16c9b8f02b007759e333b0b2621cd27bc3f60649d1aa6e'
target.write_bytes(new)
(mirror/'src/nx/gpu/shader/spirv_translate.c').write_bytes(new)

for base in [src, mirror]:
    p = base/'src/nx/gpu/vk/pipeline.c'
    s = p.read_text()
    assert s.count('direct_flow=2') == 1 and 'direct_flow=3' not in s
    p.write_bytes(s.replace('direct_flow=2', 'direct_flow=3').encode())
    t = base/'tests/shader_screen_fetch.c'
    backup = out/('shader_screen_fetch.c.before' if base == src else 'mirror-shader_screen_fetch.c.before')
    assert not backup.exists()
    shutil.copyfile(t, backup)
    t.write_bytes(Path('work/pass10-render/shader_screen_fetch.c').read_bytes().replace(b'\r\n', b'\n'))
print('Integrated narrow 2x screen-fetch fix and regressions; cache token direct_flow=3.')
