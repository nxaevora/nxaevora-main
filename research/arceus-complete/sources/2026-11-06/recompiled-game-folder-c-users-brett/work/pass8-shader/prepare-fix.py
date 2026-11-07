import sys
sys.path.pop(0)
from pathlib import Path
p=Path('work/pass8-shader')
src=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/gpu/shader/spirv_translate.c').read_text()
old='''                case O_EXIT: fall = !flow_always(insn[k]); break;
                case O_KIL:
                case O_RET:'''
new='''                case O_EXIT:
                case O_KIL: fall = !flow_always(insn[k]); break;
                case O_RET:'''
assert src.count(old)==1
fixed=src.replace(old,new)
probe='''
/* Offline pass8 test probes, not proposed for runtime integration. */
int pass8_screen_probe(const u64* w, u32 n, u32 at, u32 x, u32 y) {
    g_shader_screen_fetch = 1;
    return direct_screen_fetch(w, n, at, x, y);
}
int pass8_reach_probe(const u64* w, u32 n, u32 from, u32 use) {
    return direct_can_reach(w, n, from, use);
}
int pass8_def_probe(const u64* w, u32 n, u32 at, u32 reg) {
    return direct_reaching_def(w, n, at, reg);
}
'''
p.joinpath('spirv_translate-original.c').write_text(src+probe)
p.joinpath('spirv_translate-fixed.c').write_text(fixed+probe)
p.joinpath('proposed-change.diff').write_text('''--- spirv_translate.c
+++ spirv_translate.c
@@ direct_can_reach switch
-                case O_EXIT: fall = !flow_always(insn[k]); break;
-                case O_KIL:
+                case O_EXIT:
+                case O_KIL: fall = !flow_always(insn[k]); break;
                 case O_RET:
''')
