"""Link the traced reflection lookup into exact guest shader variants and validate each generated module."""
from pathlib import Path
import hashlib,os,re,shutil,struct,subprocess,sys,tempfile
v=Path(os.environ['WORLD_PT_PREPARED_SOURCE']).resolve()/'world-pt'
library_dir=Path(os.environ['ARCEUS_SHADER_CAPTURE_DIR']).resolve()
compiler=shutil.which('glslang') or shutil.which('glslangValidator')
def run(name,*args):
    subprocess.run([name,*args],check=True)

def asm(text,path):
    path.with_suffix('.asm').write_text(text)
    run('spirv-as','--target-env','spv1.6',path.with_suffix('.asm'),'-o',path)

def insert_before(text,op,line):
    return re.sub(r'(?m)^(\s*'+op+r'\b)',lambda m:line+'\n'+m[0],text,count=1)

def library(tmp,witness,family):
    shader=tmp/'helper.spv';source=tmp/'helper.txt'
    opts=['-DREFLECTION_CLASS='+str(family)]+(['-DFORWARD_QA'] if witness else [])
    if witness=='pair':opts.append('-DFORWARD_PAIR')
    subprocess.run([compiler,'-V','--target-env','vulkan1.3',*opts,v/'forward-reflection.frag','-o',shader],check=True)
    run('spirv-dis',shader,'-o',source);s=source.read_text()
    s=re.sub(r'^.*Op(?:EntryPoint|ExecutionMode|Name %main\b).*\n','',s,flags=re.M)
    s=re.sub(r'^\s*%main = OpFunction .*?OpFunctionEnd\n','\n',s,count=1,flags=re.M|re.S)
    defined=set(re.findall(r'(?m)^\s*(%\w+) =',s))
    s=re.sub(r'(?m)^\s*OpName (%\w+) [^\n]+',lambda m:m[0] if m[1] in defined else '',s)
    s=insert_before(s,'OpCapability','OpCapability Linkage')
    func=re.search(r'OpName (%\w+) "tracedReflection\(',s)[1]
    s=insert_before(s,'OpDecorate','OpDecorate '+func+' LinkageAttributes "world_reflection" Export')
    if witness=='pair':
        for prefix,symbol in [('comparePair','pair_enabled'),('recordPair','pair_record')]:
            target=re.search(r'OpName (%\w+) "'+prefix+r'\(',s)[1]
            s=insert_before(s,'OpDecorate','OpDecorate '+target+' LinkageAttributes "'+symbol+'" Export')
    lib=tmp/'library.spv';asm(s,lib);return lib

def guest(tmp,path,witness):
    original=tmp/'guest.txt';run('spirv-dis',path,'-o',original);s=original.read_text()
    cube=re.search(r'(%\w+) = OpTypeImage %float Cube ',s)[1]
    sampled=re.search(r'(%\w+) = OpTypeSampledImage '+cube+r'\b',s)[1]
    cubeLoads=set(re.findall(r'(%\w+) = OpLoad '+sampled+r'\b',s))
    needles=[]
    for m in re.finditer(r'(?m)^\s*(%\w+) = OpImageSampleExplicitLod %v4float (%\w+) (%\w+) Lod (%\w+)\s*$',s):
        if m[2] in cubeLoads and m[4]!='%float_0':needles.append(m)
    if len(needles)!=1:raise ValueError(f'Unexpected reflection site in {path}')
    m=needles[0];result,sampler,uv,level=m.groups()
    # These are the three components of reflect(-V,N), before the native
    # water material rotates its cubemap about Y and flips Z for sampling.
    directions={
        '26ffcf3066e0fcb1-116526e83da2c6e3':(929,943,931),
        '4efc27aeaa2cc756-ed70218598b4fb80':(755,757,760),
        '728ce0285248484c-6ac0e486d5021247':(813,840,815),
        '7f2ee087c3ad7cdd-d1655b4ecb864dcb':(763,772,770),
    }
    xyz=['%'+str(n) for n in directions[path.stem]]
    for component in xyz:
        if not re.search(re.escape(component)+r' = OpExtInst %float %\w+ Fma %\w+ %float_n2 %\w+',s[:m.start()]):
            raise ValueError(f'Native reflection expression changed in {path}: {component}')
    direction=' '.join(xyz)
    replacement=f"""
%sky = OpImageSampleExplicitLod %v4float {sampler} {uv} Lod {level}
%screenCoord = OpLoad %v4float %gl_FragCoord
OpStore %paramScreen %screenCoord
%worldDirection = OpCompositeConstruct %v3float {direction}
OpStore %paramDirection %worldDirection
OpStore %paramLevel {level}
OpStore %paramSky %sky
{result} = OpFunctionCall %v4float %reflectFn %paramScreen %paramDirection %paramLevel %paramSky"""
    s=s[:m.start()]+replacement+'\n'+s[m.end():]
    s=insert_before(s,'OpCapability','OpCapability Linkage')
    s=insert_before(s,'OpDecorate','OpDecorate %reflectFn LinkageAttributes "world_reflection" Import')
    types='';ptrs=[]
    for t in ['v4float','v3float','float']:
        match=re.search(r'(%\w+) = OpTypePointer Function %'+t+r'\b',s)
        name=match[1] if match else '%reflectPtr'+t
        if not match:types+=name+' = OpTypePointer Function %'+t+'\n'
        ptrs.append(name)
    ptr4,ptr3,ptr1=ptrs
    decl=types+f"""%reflectType = OpTypeFunction %v4float {ptr4} {ptr3} {ptr1} {ptr4}
%reflectFn = OpFunction %v4float None %reflectType
%importScreen = OpFunctionParameter {ptr4}
%importDir = OpFunctionParameter {ptr3}
%importLod = OpFunctionParameter {ptr1}
%importSky = OpFunctionParameter {ptr4}
OpFunctionEnd
"""
    idx=re.search(r'(?m)^\s*%\w+ = OpFunction ',s).start();s=s[:idx]+ '\n'+decl+s[idx:]
    main=re.search(r'OpEntryPoint Fragment (%\w+)',s)[1]
    l=re.search(re.escape(main)+r' = OpFunction .*?= OpLabel\n',s,flags=re.S)
    s=s[:l.end()]+f'%paramScreen = OpVariable {ptr4} Function\n%paramDirection = OpVariable {ptr3} Function\n%paramLevel = OpVariable {ptr1} Function\n%paramSky = OpVariable {ptr4} Function\n'+s[l.end():]
    if witness=='pair':
        from forward_pair import pair_entry
        s=pair_entry(original.read_text(),s,ptr4)
    result=tmp/'import.spv';asm(s,result);return result

def fingerprint(b):
    h=14695981039346656037
    for word, in struct.iter_unpack('<I',b):h=(h^word)*1099511628211&0xffffffffffffffff
    return h

def build(paths,witness):
    label='pair' if witness=='pair' else 'witness' if witness else 'production'
    header=[];rows=[]
    with tempfile.TemporaryDirectory(dir=library_dir) as t:
        tmp=Path(t)
        for p in paths:
            name=p.stem;h,original=name.split('-');lib=library(tmp,witness,['26ffcf3066e0fcb1','4efc27aeaa2cc756','728ce0285248484c','7f2ee087c3ad7cdd'].index(h));obj=guest(tmp,p,witness)
            out=library_dir/(name+'.'+label+'.spv')
            run('spirv-link','--target-env','vulkan1.3',obj,lib,'-o',tmp/'linked.spv')
            run('spirv-opt','--target-env=vulkan1.3','--eliminate-dead-functions','--eliminate-dead-code-aggressive','--remove-unused-interface-variables',tmp/'linked.spv','-o',out)
            run('spirv-val','--target-env','vulkan1.3',out)
            words=out.read_bytes();key='reflection_'+h+'_'+original
            header.append('static const u32 '+key+'[]={'+','.join(hex(x[0]) for x in struct.iter_unpack('<I',words))+'};')
            assert fingerprint(p.read_bytes())==int(original,16)
            rows.append('{0x'+h+'ull,0x'+original+'ull,'+str(p.stat().st_size//4)+','+key+',sizeof('+key+')}')
    header.append('static const ReflectionModule reflectionModules[]={'+','.join(rows)+'};')
    (v/('reflection-'+label+'.h')).write_text('\n'.join(header))
if __name__=='__main__':
    paths=[p for p in sorted((library_dir).glob('*.spv')) if re.fullmatch('[a-f0-9]{16}-[a-f0-9]{16}',p.stem)]
    if not paths:raise ValueError('No captured guest modules')
    for witness in (['pair'] if '--pair' in sys.argv else [False,True]):build(paths,witness)
