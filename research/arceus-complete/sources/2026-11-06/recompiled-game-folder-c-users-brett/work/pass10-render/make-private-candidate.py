import sys
sys.path.pop(0)
from pathlib import Path
import difflib,hashlib,json
root=Path('work/pass10-render')
source=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/gpu/shader/spirv_translate.c')
s=source.read_text();before=s
def replace(a,b,n=1):
 global s
 assert s.count(a)==n,(a[:80],s.count(a))
 s=s.replace(a,b)
replace('u32 screen_position, u32 position_type) {','u32 screen_position, u32 position_type, int screen_multiplier) {')
replace('u32 coord = spv_emit_vec4_extract(&c->emit, c->f32_type, position, (u32)k);', '''u32 coord = spv_emit_vec4_extract(&c->emit, c->f32_type, position, (u32)k);
            if (screen_multiplier == 2) {
                /* A proven 2*FragCoord fetch selects a full-resolution texel
                   from a half-resolution pass before integer quantization. */
                coord = spv_emit_fmul(&c->emit, c->f32_type, coord,
                    spv_emit_constant_f32(&c->emit, c->f32_type, 2.0f));
                spv_emit_no_contraction(&c->emit, coord);
            }''')
replace('''    return op == O_IPA && !B(w, 38, 1)''','''    /* Exact shipped FMUL.FTZ Rdest,Rsource,2 encoding, with only the two
       register fields variable. Reject modifiers, predication, other factors,
       post-F2I arithmetic, and repeated multiplication. */
    if (op == O_FMUL_I && (w & ~0xffffull) == 0x3868104000070000ull)
        return direct_screen_axis(insn, n, (u32)j, B(w, 8, 8), axis, 0, depth + 1) == 1 ? 2 : 0;
    return op == O_IPA && !B(w, 38, 1)''')
replace('''    return g_shader_screen_fetch && direct_screen_axis(insn, n, index, x, 0, 1, 0) &&
           direct_screen_axis(insn, n, index, y, 1, 1, 0);''','''    if (!g_shader_screen_fetch) return 0;
    int xm = direct_screen_axis(insn, n, index, x, 0, 1, 0);
    int ym = direct_screen_axis(insn, n, index, y, 1, 1, 0);
    return xm == ym ? xm : 0;''')
# Keep the direct1 output identical: calculate a compile-time proof result,
# and pass the existing position variable only for recognized chains.
replace('''u32 position = fragcoord && packed_reg == 255 &&
                    direct_screen_fetch(insn, n, i, a + array, a + array + 1) ? fragcoord : 0;''','''int screen_multiplier = fragcoord && packed_reg == 255 ?
                    direct_screen_fetch(insn, n, i, a + array, a + array + 1) : 0;
                u32 position = screen_multiplier ? fragcoord : 0;''')
replace('''u32 position = fragcoord && encoding != 4 && encoding != 12 &&
                    direct_screen_fetch(insn, n, i, xr, yr) ? fragcoord : 0;''','''int screen_multiplier = fragcoord && encoding != 4 && encoding != 12 ?
                    direct_screen_fetch(insn, n, i, xr, yr) : 0;
                u32 position = screen_multiplier ? fragcoord : 0;''')
# Call formatting is validated against the source before writing.
replace('position, vec4);','position, vec4, screen_multiplier);',2)
replace('scaled_coords(c, push, slot, 1, i32_type, &values[0], &values[1], 0, 0);','scaled_coords(c, push, slot, 1, i32_type, &values[0], &values[1], 0, 0, 0);')
(root/'spirv_translate-before.c').write_text(before)
(root/'spirv_translate.c').write_text(s)
(root/'screen-2x.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),s.splitlines(True),fromfile='spirv_translate.c',tofile='spirv_translate.c')))
(root/'candidate-provenance.json').write_text(json.dumps(dict(source=str(source),before_sha256=hashlib.sha256(before.encode()).hexdigest(),candidate_sha256=hashlib.sha256(s.encode()).hexdigest()),indent=2))
print('wrote private candidate')
