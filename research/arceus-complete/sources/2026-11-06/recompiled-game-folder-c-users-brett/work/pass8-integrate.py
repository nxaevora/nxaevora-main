import sys
sys.path.pop(0)
from pathlib import Path
import hashlib, json, shutil, difflib

root = Path.cwd()
src = Path('C:/Users/brett/Documents/Codex/work/nvn-src')
mirror = Path('C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main/third_party/ScarletNX')
out = root / 'work/pass8-integration'
out.mkdir(exist_ok=True)
changes = {
 'src/nx/gpu/shader/spirv_translate.c': [
  ('case O_EXIT: fall = !flow_always(insn[k]); break;\n                case O_KIL:',
   'case O_EXIT:\n                case O_KIL: fall = !flow_always(insn[k]); break;'),
  ('    if (op == O_MOV_R)\n        return direct_screen_axis(insn, n, (u32)j, B(w, 20, 8), axis, integer, depth + 1);',
   '    if (op == O_MOV_R) {\n        /* Match the emitter: other masks do not assign this register. */\n        if (B(w, 39, 4) != 1 && B(w, 39, 4) != 15) return 0;\n        return direct_screen_axis(insn, n, (u32)j, B(w, 20, 8), axis, integer, depth + 1);\n    }')],
 'src/nx/gpu/vk/pipeline.c': [
  ('vertex_bounds=%d screen_fetch=%d"', 'vertex_bounds=%d screen_fetch=%d direct_flow=2"')]
}
manifest = {}
diff = []
for rel, edits in changes.items():
 p = src / rel
 original = p.read_bytes()
 old = original.decode().replace('\r\n','\n')
 new = old
 for a,b in edits:
  assert new.count(a) == 1, (rel,a)
  new = new.replace(a,b)
 backup = out / (p.name + '.before')
 assert not backup.exists(), backup
 backup.write_bytes(original)
 if (mirror/rel).exists():
  assert (mirror/rel).read_bytes() == original, ('mirror differs',rel)
 p.write_text(new, newline='\n')
 (mirror/rel).write_bytes(p.read_bytes())
 manifest[rel] = dict(before=hashlib.sha256(original).hexdigest(), after=hashlib.sha256(p.read_bytes()).hexdigest())
 diff.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='before/'+rel,tofile='after/'+rel))
candidate = root/'work/ArceusEngineCandidate-runtime.exe'
backup = root/'work/ArceusEngineCandidate-before-pass8-runtime.exe'
assert not backup.exists()
shutil.copy2(candidate,backup)
manifest['runtime_before'] = hashlib.sha256(backup.read_bytes()).hexdigest()
(out/'source-changes.diff').write_text(''.join(diff))
(out/'integration.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps(manifest,indent=2))
