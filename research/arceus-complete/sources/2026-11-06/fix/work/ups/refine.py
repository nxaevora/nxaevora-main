from pathlib import Path
p=Path(__file__).resolve().parent
b=p/'build.py';s=b.read_text().replace("s=s.replace(\"    DrawImages images", "s=s.replace('vk_query_draw(cb, ch->id, r[0x574]);','vk_query_draw(cb, ch->id, r[0x574] | (raster_scale>1.1f?0x80000000u:0));')\ns=s.replace(\"    DrawImages images")
i=s.index("v='nx/gpu/vk/device.c'")
s=s[:i]+'''qsrc=(srcroot/'nx/gpu/vk/query.c').read_text()
qsrc=qsrc.replace('static u8 g_qown[QN], g_qmul[QN];','static u8 g_qown[QN], g_qmul[QN], up_display[QN];').replace('msaa_samples(msaa_mode);','msaa_samples(msaa_mode&0x7fffffffu);').replace('g_qmul[at] == mul)', 'g_qmul[at] == mul && up_display[at]==(msaa_mode>>31))').replace('g_qmul[at] = (u8)mul;','g_qmul[at] = (u8)mul; up_display[at]=msaa_mode>>31;')
qsrc=qsrc.replace('snx_le64(g_qres[s % QN]) * g_qmul[s % QN]', '(up_display[s % QN] ? (snx_le64(g_qres[s % QN])*g_qmul[s % QN]*9+8)/16 : snx_le64(g_qres[s % QN])*g_qmul[s % QN])').replace('double area = (double)g_vk_scale * (double)g_vk_scale;','double area=1.0;')
(p/'query.c').write_text(qsrc);cc('/src/nx/gpu/vk/query.c','query.c','query.obj')
'''+s[i:]
s=s.replace("'render.obj','common.obj'","'render.obj','query.obj','common.obj'")
b.write_text(s)
f=p/'split.inc';s=f.read_text();a=s.index('static void tjitter(');b=s.index('static int tinit',a);s=s[:a]+'''static float up_halton(u32 x,u32 b){float v=0,a=1;while(x){a/=b;v+=a*(x%b);x/=b;}return v;}
static void tjitter(float *xy){u32 phase=1+g_vk_present%14;xy[0]=up_halton(phase,2)-.5f;xy[1]=up_halton(phase,3)-.5f;}
'''+s[b:];s=s.replace('for(int i=0;i<3;i++)tuse(cb,&th[i/2][i%2],rw);','for(int i=0;i<3;i++)tuse(cb,&th[i/2][i%2],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);');f.write_text(s)
f=p/'fsr.cpp';s=f.read_text().replace('const uint32_t rd = FFX_API_RESOURCE_STATE_GENERIC_READ;', 'const uint32_t rd = FFX_API_RESOURCE_STATE_COMPUTE_READ;');f.write_text(s)
