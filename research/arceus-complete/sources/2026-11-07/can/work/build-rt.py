from pathlib import Path
import json,shlex,subprocess,hashlib,re,shutil,struct
here=Path.cwd()
tmp=here/'work/rt'
out=here/'outputs'
base=Path.home()/'Documents/Codex'
fix=base/'2026-11-06/fix'
ups=fix/'work/ups'
shared=base/'work'
build=shared/'nvn-build'
compiler=next((shared/'nvn-src/.cache/cpm/glslang-tools').glob('*/bin/glslang.exe'))
header=[]
for name,flags in [('mesh',['-DBUILD_MESH']),('contact',[])]:
    spv=tmp/(name+'.spv')
    subprocess.run([compiler,'-V','--target-env','vulkan1.2',*flags,'-o',spv,tmp/'contact.comp'],check=True)
    header.append('static const u32 '+name+'_words[]={'+','.join(hex(w[0]) for w in struct.iter_unpack('<I',spv.read_bytes()))+'};')
(tmp/'contact_words.h').write_text('\n'.join(header))
device=(ups/'device.c').read_text()
device=device.replace('int g_vk_preserve_infnan;', 'extern int arceus_contact_supported;\nint arceus_contact_supported;\nint g_vk_preserve_infnan;')
device=device.replace('    VkDeviceCreateInfo dci =', '''    const char *contact_exts[]={VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,VK_KHR_RAY_QUERY_EXTENSION_NAME,VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
    VkPhysicalDeviceRayQueryFeaturesKHR rq={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accel={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,.pNext=&rq};
    VkPhysicalDeviceFeatures2 probe={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&accel};
    SnxVkExtensions available={0};
    int supported=q12.bufferDeviceAddress && snx_vk_device_extensions(g_vk.phys,&available);
    for(unsigned i=0;i<3 && supported;i++)supported=snx_vk_extensions_has(&available,contact_exts[i]);
    snx_vk_extensions_free(&available);
    if(supported && next+3<=sizeof g_vk.dev_ext/sizeof *ext){
        vkGetPhysicalDeviceFeatures2(g_vk.phys,&probe);
        if(accel.accelerationStructure && rq.rayQuery){
            for(unsigned i=0;i<3;i++){
                unsigned j=0;while(j<next && strcmp(ext[j],contact_exts[i]))++j;
                if(j==next)ext[next++]=contact_exts[i];
            }
            f12.bufferDeviceAddress=VK_TRUE;
            rq.pNext=chain;
            accel=(VkPhysicalDeviceAccelerationStructureFeaturesKHR){.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,.pNext=&rq,.accelerationStructure=VK_TRUE};
            chain=&accel;
            arceus_contact_supported=1;
        }
    }
    LOG("arceus-rt: Vulkan ray queries %s\\n",arceus_contact_supported?"enabled":"unavailable");
    VkDeviceCreateInfo dci =''')
render=(here/'work/render.c').read_text().replace('static void upscale_before(VkSh *fs,TexRes *tex,Surf *target){','#include "contact.h"\nstatic void upscale_before(VkSh *fs,TexRes *tex,Surf *target){')
render=render.replace(' bool dl=vk_dlaa_wanted();',' contact_record(cb,set,cur);\n bool dl=vk_dlaa_wanted();')
assert render.count('contact_record(cb,set,cur)')==1
db=json.loads((build/'compile_commands.json').read_text())
for name,source in [('render',render),('device',device)]:
    f=tmp/(name+'.c')
    f.write_text(source)
    row=next(x for x in db if x['file'].endswith('/nx/gpu/vk/'+name+'.c'))
    args=[v.strip('"') for v in shlex.split(row['command'],posix=False)]
    args[args.index('-o')+1]=str(tmp/(name+'.obj'));args[-1]=str(f)
    subprocess.run(args+['-I'+str(ups)],cwd=build,check=True)
s=(here/'work/build-merged.py').read_text().split('args=json.loads((fix/',1)[1]
s='args=json.loads((fix/'+s
s=s.replace("str(tmp/'query.obj')","str(here/'work/query.obj')").replace("str(ups/f) for f", "str((tmp if f=='device.obj' else ups)/f) for f").replace('village-v3-4k-runtime','village-rt-runtime').replace('Arceus-Village-v3-DLSS-4K.exe','Arceus-Village-v3-RT-DLSS-4K.exe').replace('current-village-v3-DLSS-4K-test','current-village-RT-DLSS-4K-test')
exec(s)
