import sys
sys.path.pop(0)
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

here = Path(__file__).resolve().parent
build = Path('C:/Users/brett/Documents/Codex/work/nvn-build')
source = Path('C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/gpu/vk/surface.c')
snapshot = here / 'surface-baseline.c'
if not snapshot.exists():
    original = source.read_bytes()
    if b'void vk_surf_diagnostic_capture(' in original:
        raise RuntimeError('Production helper already integrated; original private baseline is required')
    snapshot.write_bytes(original)
entry = next(x for x in json.loads((build / 'compile_commands.json').read_text())
             if Path(x['file']) == source)
unit = here / 'surface-private.c'
unit.write_text('#include "nx/gpu/vk/vk.h"\nvoid vk_surf_diagnostic_capture(const char* prefix);\n'
                + '#include "' + snapshot.as_posix() + '"\n'
                + '#include "surface-capture.inc"\n')
raw = [x.removeprefix('"').removesuffix('"') for x in shlex.split(entry['command'], posix=False)]
args = []
i = 0
while i < len(raw):
    if raw[i] in ('-o', '-c'):
        i += 2
    else:
        args.append(raw[i])
        i += 1
args += ['-Wall', '-Wextra', '-c', str(unit), '-o', str(here / 'surface-private.obj')]
result = subprocess.run(args, cwd=entry['directory'], text=True, capture_output=True)
(here / 'compile-result.txt').write_text(result.stdout + result.stderr)
record = {'returncode': result.returncode,
          'helper_sha256': hashlib.sha256((here / 'surface-capture.inc').read_bytes()).hexdigest(),
          'surface_source_sha256': hashlib.sha256(snapshot.read_bytes()).hexdigest(),
          'command': args, 'output': result.stdout + result.stderr}
(here / 'compile-provenance.json').write_text(json.dumps(record, indent=2))
print(json.dumps(record, indent=2))
raise SystemExit(result.returncode)
