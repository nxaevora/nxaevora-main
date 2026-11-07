from pathlib import Path
import struct,subprocess
p=Path(__file__).resolve().parent
compiler=next(Path('C:/Users/brett/Documents/Codex/work/nvn-src/.cache/cpm/glslang-tools').glob('*/bin/glslang.exe'))
src=['/* Generated from the material motion compute shaders. */']
for name in ['measure','candidates','score','choose','validate','apply','seed']:
    out=p/(name+'.spv')
    subprocess.run([compiler,'-V','--target-env','vulkan1.2','-I'+str(p),'-o',out,p/(name+'.comp')],check=True)
    words=struct.iter_unpack('<I',out.read_bytes())
    src.extend(['static const uint32_t '+name+'_spv[]={',','.join(hex(x[0]) for x in words),'};'])
(p/'flow_words.h').write_text('\n'.join(src)+'\n')
