from pathlib import Path
import subprocess, os, json
import numpy as np

root = Path(__file__).resolve().parent
workspace = root.parent.parent
# Use the local Vulkan toolchain.
shared = Path('C:/Users/brett/Documents/Codex/work')
runtime = workspace / 'outputs/upscale-runtime'
compiler = shared / 'llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe'
headers = next((shared / 'nvn-src/.cache/cpm/vulkan-headers').glob('*/include'))
testdir = root / 'motion-test'
testdir.mkdir(exist_ok=True)
# Use the existing Vulkan fixture's device setup, vendor entry points and full-image readback.
source = (root / 'check.c').read_text()
start = source.index('if(replay){for(int i=0;i<2;i++){')
end = source.index('VkMemoryBarrier mb=', start)
upload = source[start:end].replace('if(replay){for(int i=0;i<2;i++){','for(int i=0;i<3;i++){').replace('FILE*fi=fopen(argv[i+2],"rb");','char file[128]; snprintf(file,sizeof file,"%s-%d.bin",prefix,i);FILE*fi=fopen(file,"rb");').replace(' }}',' }')
source = source[:start] + source[end:]
start = source.index('for(int i=0;i<3;i++){VkImageMemoryBarrier ib=')
end = source.index('if(!(dl?vk_dlaa_record', start)
prepare_read = source[start:source.index('for(int tick=',start)]
source = source[:start] + '''
const VkImageLayout readLayout=dl?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
for(int tick=0;tick<3;tick++){
 const bool changed=tick==2;
 const bool cold=!strcmp(argv[2],"cold");
 const char*prefix=(changed||cold)?argv[3]:"original";
 frame.reset=tick==0||(changed&&!strcmp(argv[2],"reset"))||cold;
 for(int i=0;i<3;i++){
    VkImageMemoryBarrier writable={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
       .srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
       .oldLayout=tick?readLayout:VK_IMAGE_LAYOUT_GENERAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
       .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
       .image=imgs[i],.subresourceRange=sub};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,0,0,0,1,&writable);
 }
''' + upload + prepare_read + source[end:]
(root/'motion_check.c').write_text(source)
exe = runtime/'MotionReplay.exe'
subprocess.run([compiler,'-O2','-I'+str(headers),root/'motion_check.c',root/'ngx.o',root/'fsr.o','C:/Windows/System32/vulkan-1.dll','-ladvapi32','-o',exe],check=True)
color=np.fromfile(root/'scene.raw','<f2').reshape(1080,1920,4)
depth=np.fromfile(root/'depth.raw','<f4').reshape(1080,1920)
# Shift the image right by 24 render pixels, exactly 32 output pixels, with edge replication.
x=np.maximum(0,np.arange(1920)-24)
for prefix in ('original','pan','zero','cut'):
    rgb=color if prefix=='original' else color[:,x]
    if prefix=='cut':
        rgb=color[:,::-1].copy(); rgb[:,:,:3]=rgb[:,:,[2,0,1]]
    z=depth if prefix=='original' else depth[:,::-1] if prefix=='cut' else depth[:,x]
    motion=np.zeros((1080,1920,2),'<f2')
    if prefix=='pan':motion[:,:,0]=-24
    for i,array in enumerate((rgb,z,motion)):array.tofile(testdir/f'{prefix}-{i}.bin')

def display(rgb):
    rgb=rgb.astype(np.float32)
    assert np.isfinite(rgb).all()
    return (np.maximum(rgb,0)/(1+np.maximum(rgb,0)))**(1/2.2)

report={}
for backend in ('FSR','DLSS'):
    def run(mode,frame):
        dest=testdir/'readback.bin'
        r=subprocess.run([exe,backend,mode,frame,dest],cwd=testdir,capture_output=True,text=True)
        if r.returncode:raise RuntimeError((r.returncode,r.stdout,r.stderr))
        return display(np.fromfile(dest,'<f2').reshape(1440,2560,4)[24:-24,64:-64,:3])
    base=display(np.fromfile(root/f'{backend}.raw','<f2').reshape(1440,2560,4)[24:-24,32:-96,:3])
    correct=run('keep','pan');untracked=run('keep','zero')
    cut=run('reset','cut');cold=run('cold','cut')
    result={'mean_pan_error_correct_motion':float(np.abs(correct-base).mean()),'mean_pan_error_zero_motion':float(np.abs(untracked-base).mean()),'mean_reset_vs_cold_difference':float(np.abs(cut-cold).mean())}
    assert result['mean_pan_error_correct_motion'] < result['mean_pan_error_zero_motion'] * 0.5, result
    assert result['mean_reset_vs_cold_difference'] < 0.0001, result
    print(backend,result,flush=True);report[backend]=result
(testdir/'results.json').write_text(json.dumps(report,indent=2))

