from pathlib import Path
import subprocess,json,struct,hashlib
root=Path(__file__).resolve().parent.parent;out=root/'work/reflection'
base=Path('C:/Users/brett/Documents/Codex/2026-11-06/recompiled-game-folder-c-users-brett/work/native-shadow-candidate')
build=Path('C:/Users/brett/Documents/Codex/work/nvn-build')
(out/'baseline-check').mkdir(exist_ok=True)
relink=out/'baseline-check/ArceusNativeShadow-runtime.exe'
args=json.loads((base/'link-arguments.json').read_text());args[args.index('-o')+1]=str(relink)
p=subprocess.run(args,cwd=build,capture_output=True,text=True);assert p.returncode==0,p.stderr
def sections(path):
 b=path.read_bytes();pe=struct.unpack_from('<I',b,60)[0];count=struct.unpack_from('<H',b,pe+6)[0];opt=struct.unpack_from('<H',b,pe+20)[0];pos=pe+24+opt;ans={}
 for i in range(count):
  at=pos+40*i;name=b[at:at+8].rstrip(b'\0').decode();size,ptr=struct.unpack_from('<II',b,at+16)
  if name in ('.text','.rdata','.data'):ans[name]=hashlib.sha256(b[ptr:ptr+size]).hexdigest()
 return ans
a=sections(base/'ArceusNativeShadow-runtime.exe');b=sections(relink)
assert a==b,(a,b)
# Execute the actual new size helper with mocked metadata, no Vulkan device or game.
s=(out/'surface.c').read_text();part=s[s.index('static u32 reflection_block_bytes'):s.index('static void vk_surf_reflection_capture(const char* prefix) {')]
formats=sorted(set(__import__('re').findall(r'VK_FORMAT_\w+',part)))
prefix='''#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t u32; typedef uint64_t u64; typedef int VkImageAspectFlags;
enum { VK_IMAGE_ASPECT_DEPTH_BIT=1,VK_IMAGE_ASPECT_COLOR_BIT=2,VK_IMAGE_LAYOUT_UNDEFINED=0,VK_IMAGE_VIEW_TYPE_2D=1,VK_IMAGE_VIEW_TYPE_2D_ARRAY=2,VK_IMAGE_VIEW_TYPE_CUBE=3,VK_IMAGE_VIEW_TYPE_CUBE_ARRAY=4,VK_IMAGE_VIEW_TYPE_3D=5 };
typedef struct { int img,is_rt,took_drawn,layout,pred_undefined,volume,type,depth3d; u32 sw,sh,layers;u64 last_used; struct {int depth,vk;} f;} Surf;
#define DIAG_SURF_IMAGE_BYTES (64ull<<20)
'''+ 'enum {'+','.join(formats)+'};\n'
test='''
int main(void) {
 Surf s={.img=1,.layout=1,.type=VK_IMAGE_VIEW_TYPE_CUBE,.depth3d=1,.sw=64,.sh=64,.layers=6,.last_used=100,.f.vk=VK_FORMAT_BC6H_SFLOAT_BLOCK};
 u32 b;u64 size,sum=0;VkImageAspectFlags a;
 u64 expected[]={24576,6144,1536,384,96,96,96};
 for(u32 m=0;m<7;m++){assert(reflection_surface_size(&s,100,m,&b,&a,&size));assert(b==16&&size==expected[m]);sum+=size;}
 assert(sum==32928);
 s.f.vk=VK_FORMAT_BC6H_UFLOAT_BLOCK;assert(reflection_surface_size(&s,100,6,&b,&a,&size)&&size==96);
 s.f.vk=VK_FORMAT_R16G16B16A16_SFLOAT;assert(reflection_surface_size(&s,100,0,&b,&a,&size)&&size==196608);
 s.type=VK_IMAGE_VIEW_TYPE_2D;assert(!reflection_surface_size(&s,100,0,&b,&a,&size));s.type=VK_IMAGE_VIEW_TYPE_CUBE;
 s.last_used=90;assert(!reflection_surface_size(&s,100,0,&b,&a,&size));s.last_used=100;
 s.layers=0;assert(!reflection_surface_size(&s,100,0,&b,&a,&size));s.layers=6;
 s.layout=0;assert(!reflection_surface_size(&s,100,0,&b,&a,&size));s.layout=1;
 s.sw=65536;s.sh=65536;assert(!reflection_surface_size(&s,100,0,&b,&a,&size));
 puts("PASS: actual C size helper: BC6 signed/unsigned, tiny mips, six faces, uncompressed, stale/noncube/undefined/oversize rejection");
}
'''
(out/'size-test.c').write_text(prefix+part+test)
cc=Path('C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe')
p=subprocess.run([str(cc),str(out/'size-test.c'),'-O2','-Wall','-Wextra','-o',str(out/'size-test.exe')],capture_output=True,text=True);assert p.returncode==0,p.stderr
p=subprocess.run([str(out/'size-test.exe')],capture_output=True,text=True);assert p.returncode==0,p.stdout+p.stderr
proof={'baseline_relink_sections_identical':a,'size_tests':p.stdout,'game_launched':False}
(out/'verification.json').write_text(json.dumps(proof,indent=2));print(json.dumps(proof,indent=2))
