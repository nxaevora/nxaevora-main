from pathlib import Path
import shutil
root=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src')
paths=['src/nx/gpu/shader/spirv_translate.c','src/nx/gpu/shader/shader.h','src/nx/gpu/vk/pipeline.c','tools/snx_shaderc.c']
for name in paths:shutil.copy2(root/name,Path('work')/(Path(name).name+'.before-screen-fetch'))
p=root/paths[0];s=p.read_text()
s=s.replace('int g_shader_maximal_reconvergence;','int g_shader_maximal_reconvergence;\nint g_shader_screen_fetch;')
old='static void scaled_coords(DirectCtx* c, u32 push, int slot, int size, u32 i32_type, u32* x, u32* y) {'
new='static void scaled_coords(DirectCtx* c, u32 push, int slot, int size, u32 i32_type, u32* x, u32* y,\n                          u32 screen_position, u32 position_type) {'
assert old in s;s=s.replace(old,new)
anchor='''    u32* v[2] = {x, y};
    for (int k = 0; k < 2; k++) {'''
replacement='''    u32* v[2] = {x, y};
    u32 position = screen_position ? spv_emit_load(&c->emit, position_type, screen_position) : 0;
    for (int k = 0; k < 2; k++) {'''
assert s.count(anchor)==1;s=s.replace(anchor,replacement)
anchor='''                                            spv_emit_glsl_unary(&c->emit, c->f32_type, c->glsl, SPV_GLSL_FLOOR, f));
    }
}

static void vertex_depth_fix'''
replacement='''                                            spv_emit_glsl_unary(&c->emit, c->f32_type, c->glsl, SPV_GLSL_FLOOR, f));
        if (position && !size) {
            /* Proven F2I(FragCoord) reads the current screen texel. Scaling its
               already-rounded guest coordinate duplicates/skips higher-resolution
               samples. For a scaled image, use the actual framebuffer pixel;
               unscaled textures retain the game's integer coordinate. */
            u32 coord = spv_emit_vec4_extract(&c->emit, c->f32_type, position, (u32)k);
            coord = spv_emit_glsl_unary(&c->emit, c->f32_type, c->glsl, SPV_GLSL_FLOOR, coord);
            u32 exact = spv_emit_convert_f_to_s(&c->emit, i32_type, coord);
            *v[k] = spv_emit_select(&c->emit, i32_type,
                spv_emit_i_not_equal(&c->emit, c->bool_type, bit, c->zero), exact, *v[k]);
        }
    }
}

static void vertex_depth_fix'''
assert s.count(anchor)==1;s=s.replace(anchor,replacement)
anchor='static int direct_track_const('
helper=r'''/* Conservative proof of a current-screen-pixel fetch. Follow MOV and the
   existing dominating-definition analysis; do not infer screen space from a
   coincidental coordinate value. Offsets, arbitrary UV arithmetic, modified
   coordinates, other rounding modes and ambiguous reaching definitions keep
   the original resolution-scale behavior. */
static int direct_screen_axis(const u64* insn, u32 n, u32 index, u32 reg, u32 axis, int integer, int depth) {
    int j = depth > 8 ? -1 : direct_reaching_def(insn, n, index, reg);
    if (j < 0) return 0;
    u64 w = insn[j];
    int op = decode(w)->id;
    if (op == O_MOV_R)
        return direct_screen_axis(insn, n, (u32)j, B(w, 20, 8), axis, integer, depth + 1);
    if (integer) {
        if (op != O_F2I_R || B(w, 8, 2) != 2 || B(w, 10, 2) != 2 || B(w, 49, 1) || B(w, 45, 1) ||
            (B(w, 39, 2) != 1 && B(w, 39, 2) != 3)) return 0;
        return direct_screen_axis(insn, n, (u32)j, B(w, 20, 8), axis, 0, depth + 1);
    }
    return op == O_IPA && !B(w, 38, 1) && !B(w, 51, 1) && B(w, 54, 2) != 1 &&
           B(w, 30, 8) * 4 == 0x70 + 4 * axis;
}

static int direct_screen_fetch(const u64* insn, u32 n, u32 index, u32 x, u32 y) {
    return g_shader_screen_fetch && direct_screen_axis(insn, n, index, x, 0, 1, 0) &&
           direct_screen_axis(insn, n, index, y, 1, 1, 0);
}

'''
assert s.count(anchor)==1;s=s.replace(anchor,helper+anchor)
old='            if (type == 1 && needs_texel_scale) scaled_coords(c, push, slot, 0, i32_type, &x, &y);'
assert s.count(old)==2
new='''            if (type == 1 && needs_texel_scale) {
                u32 position = fragcoord && packed_reg == 255 &&
                    direct_screen_fetch(insn, n, i, a + array, a + array + 1) ? fragcoord : 0;
                scaled_coords(c, push, slot, 0, i32_type, &x, &y, position, vec4);
            }'''
s=s.replace(old,new,1)
new='''            if (type == 1 && needs_texel_scale) {
                u32 xr = array ? b : a;
                u32 yr = array ? b + 1 : encoding == 4 || encoding == 5 || encoding == 6 ||
                         encoding == 7 || encoding == 12 ? a + 1 : b;
                u32 position = fragcoord && encoding != 4 && encoding != 12 &&
                    direct_screen_fetch(insn, n, i, xr, yr) ? fragcoord : 0;
                scaled_coords(c, push, slot, 0, i32_type, &x, &y, position, vec4);
            }'''
s=s.replace(old,new,1)
s=s.replace('scaled_coords(c, push, slot, 1, i32_type, &values[0], &values[1]);','scaled_coords(c, push, slot, 1, i32_type, &values[0], &values[1], 0, 0);')
p.write_text(s)
p=root/paths[1];s=p.read_text();s=s.replace('extern int g_shader_maximal_reconvergence;','extern int g_shader_maximal_reconvergence;\n/* Opt-in precise texel fetches proven to read F2I(FragCoord); part of the shader-cache key. */\nextern int g_shader_screen_fetch;');p.write_text(s)
p=root/paths[2];s=p.read_text();s=s.replace('void vk_pipeline_init(void) {','''void vk_pipeline_init(void) {
    const char* screen_fetch = getenv("SNX_ARCEUS_SCREEN_FETCH");
    g_shader_screen_fetch = screen_fetch && !strcmp(screen_fetch, "1");
    if (g_shader_screen_fetch) LOG("vulkan: precise current-screen-pixel texture fetch candidate enabled\\n");''')
s=s.replace('vertex_bounds=%d",','vertex_bounds=%d screen_fetch=%d",').replace('(unsigned)sizeof(PfShader), g_vk.has_vertex_robustness2);','(unsigned)sizeof(PfShader), g_vk.has_vertex_robustness2, g_shader_screen_fetch);');p.write_text(s)
p=root/paths[3];s=p.read_text();s=s.replace('else if (!strcmp(argv[i], "--no-flush"))','else if (!strcmp(argv[i], "--screen-fetch"))\n            g_shader_screen_fetch = 1;\n        else if (!strcmp(argv[i], "--no-flush"))');s=s.replace('[--dispatch-reconverge]\\n','[--dispatch-reconverge] [--screen-fetch]\\n');p.write_text(s)
