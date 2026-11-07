from pathlib import Path
roots=[Path(r'C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main/third_party/ScarletNX'),Path(r'C:/Users/brett/Documents/Codex/work/nvn-src')]
for root in roots:
 for ext in ['c','h']:(root/f'src/nx/runtime/arceus_clock.{ext}').write_text(Path(f'work/arceus_clock.{ext}').read_text())
 p=root/'src/nx/runtime/main.c';s=p.read_text();s=s.replace('#include "nx/runtime/main.h"','#include "nx/runtime/main.h"\n#include "nx/runtime/arceus_clock.h"',1);s=s.replace('    nvn_trace_init();','    nvn_trace_init();\n    arceus_clock_init();',1);p.write_text(s)
 p=root/'src/nx/CMakeLists.txt';s=p.read_text().replace('runtime/cheats.c)','runtime/cheats.c runtime/arceus_clock.c)',1);p.write_text(s)
