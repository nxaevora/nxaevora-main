from pathlib import Path
import hashlib,json,re,shutil,subprocess,sys,runpy,tempfile,os
root=Path.cwd()
stage=Path(sys.argv[1]).resolve()
w=root/'work/v2'
out=root/'outputs'
def check_layer(log,logfile):
    known=("has an Output value declared at Location","and this write is unused","which is the duplicate_message_limit value")
    for line in log.splitlines():
        if ("vulkan validation:" in line and not any(msg in line for msg in known)) or re.search(r"THREADING ERROR|VUID-[A-Za-z]|vendor dispatch failed|device lost|undefined values",line,re.I):
            raise ValueError(f"{logfile.name}: {line}")

# The validation callback can report an error without a VUID.
for api in ("DLSS","FSR"):
    for slots in (0,16):
        logfile=w/f"validate-{stage.name}-{api}-{slots}.log"
        log=logfile.read_text(errors="replace")
        required=["frames=2208 hash=","temporal radiance filter ready",
                  "arceus-rt: "+str(slots or 192)+" material texture slots",
                  ("DLSS" if api=="DLSS" else "FSR 3.1.6")+" pre-HUD 1920x1080 -> 3840x2160"]
        if api=="DLSS": required.append("ModelPreset, Value: 13, Requested: Yes, Applied: Yes")
        if any(item not in log for item in required) or not re.search(r"retainedPbrHits=[1-9]",log):
            raise ValueError(f"Incomplete validation in {logfile.name}")
        check_layer(log,logfile)
        if not re.search(r"shader_cache_world_pt_[a-f0-9]{16}",log):
            raise ValueError(f"Missing isolated shader cache in {logfile.name}")
manifest=stage/"validated-inputs.json"
terrain=Path(sys.argv[2]).resolve()
current=runpy.run_path(str(w/"validation-inputs.py"))["package_files"](stage,terrain)
if current!=json.loads(manifest.read_text()):
    raise ValueError("Candidate files changed during validation")
if '--rr' in sys.argv[3:]:
    rrlog=(w/f"validate-{stage.name}-RR-0.log").read_text(errors='replace')
    check_layer(rrlog,w/f"validate-{stage.name}-RR-0.log")
    if not all(key in rrlog for key in ('arceus-rr: Ray Reconstruction created 1920x1080 -> 3840x2160',
                    'arceus-rr: evaluated raw radiance at present 2160','frames=2208 hash=')):
        raise RuntimeError('Missing RR evaluation during the validation route')
    subprocess.run([sys.executable,w/"check-rr-guides.py",w/f"filter-validate-{stage.name}-RR-0/rr-guides.bin"],check=True)
if '--fallback-stage' in sys.argv:
    name=sys.argv[sys.argv.index('--fallback-stage')+1]
    logpath=w/f'validate-{name}-RR-fallback-16.log'
    fallback=logpath.read_text(errors="replace")
    check_layer(fallback,logpath)
    if not all(t in fallback for t in ('feature creation failed','temporal radiance filter ready',
             'ModelPreset, Value: 13, Requested: Yes, Applied: Yes','frames=2208 hash=')):
        raise ValueError('Missing verified fallback route')
# Install each tested binary/library/terrain combination in its own directory.
# Previously distributed launchers can still verify and start their original build.
key=hashlib.sha256(json.dumps(current,sort_keys=True).encode()).hexdigest()[:20]
runtime=out/f'world-pt-v3-runtime-{key}'
candidate=stage/'world-pt-v2-runtime/ArceusTest.exe'
exe=runtime/'ArceusTest.exe'
def digest(file):
    with file.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').digest()
with tempfile.TemporaryDirectory(prefix='.package-',dir=out) as temporary:
    if not runtime.exists():
        staged=Path(temporary)/'runtime'
        for name,expected in current.items():
            source=(terrain/Path(name).name if name.startswith('terrain/') else candidate.parent/name)
            target=staged/name
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(source,target)
            if digest(target).hex()!=expected:
                raise RuntimeError(f'Packaging changed {name}')
        try: staged.rename(runtime)
        except FileExistsError: pass
for name,h in current.items():
    if digest(runtime/name).hex()!=h:
        raise RuntimeError(f'Installed resource differs: {name}')
base=Path.home()/'Documents/Codex/work'
src=(w/'launcher.c').read_text()
escaped=lambda p:str(p).replace(chr(92),chr(92)*2)
src=re.sub(r'^#define DIAGNOSTIC .*$',lambda _:'#define DIAGNOSTIC L"'+escaped(exe)+'"',src,flags=re.M)
src=re.sub(r'env\(L"ARCEUS_TERRAIN_DIR",L"[^"]+"\)',lambda _:'env(L"ARCEUS_TERRAIN_DIR",L"'+escaped(runtime/'terrain')+'")',src)
h=digest(candidate)
assert digest(exe)==h
src=re.sub(r'(static int diagnostic_ok\(void\)\{static const unsigned char h\[32\]=\{)[^}]+',lambda m:m[1]+','.join(hex(x) for x in h),src)
src=src.replace('Village v3 DLSS 4K','World PT v3 DLSS 4K').replace('current-world-pt-v2','current-world-pt-v3')
src=src.replace(r'\"temporal_backend\":\"DLSS\"',r'\"temporal_backend\":\"DLSS\",\"quality_mode\":\"Performance\",\"requested_model\":\"M\",\"scene_width\":1920,\"scene_height\":1080,\"output_width\":3840,\"output_height\":2160')
fsr=src.replace('World PT v3 DLSS 4K','World PT v3 FSR 4K').replace('current-world-pt-v3.txt','current-world-pt-v3-fsr.txt')
fsr=fsr.replace('env(L"ARCEUS_UPSCALER",L"DLSS")','env(L"ARCEUS_NGX",L"0");env(L"ARCEUS_UPSCALER",L"FSR")')
src=src.replace('env(L"ARCEUS_UPSCALER",L"DLSS")','env(L"ARCEUS_NGX",L"1");env(L"ARCEUS_UPSCALER",L"DLSS")')
fsr=fsr.replace(r'\"temporal_backend\":\"DLSS\"',r'\"temporal_backend\":\"FSR 3.1.6\"').replace(r',\"requested_model\":\"M\"','')
launchers=[('',src),('-FSR',fsr)]
launchers=[(suffix,code.replace('env(L"ARCEUS_NGX",', 'env(L"ARCEUS_DLSS_RR",L"0");env(L"ARCEUS_NGX",'))
           for suffix,code in launchers]
if '--rr' in sys.argv[3:]:
    rr=src.replace('World PT v3 DLSS 4K','World PT v3 DLSS RR 4K').replace('current-world-pt-v3.txt','current-world-pt-v3-rr.txt')
    rr=rr.replace('env(L"ARCEUS_NGX",','env(L"ARCEUS_DLSS_RR",L"1");env(L"ARCEUS_NGX",')
    rr=rr.replace(r'\"requested_model\":\"M\"',r'\"ray_reconstruction_requested\":true,\"sr_fallback_model\":\"M\"')
    launchers.append(('-RR',rr))
with tempfile.TemporaryDirectory(prefix='.launchers-',dir=out) as temporary:
    verified=[]
    for suffix,code in launchers:
        launcher=Path(temporary)/f'Arceus-World-PT-v3{suffix}-4K.exe'
        source=Path(temporary)/f'launcher{suffix}.c'
        source.write_text(code)
        subprocess.run([base/'llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe',source,'-O2','-municode','-mwindows','-DSHADOWMODE=1','-lbcrypt','-o',launcher],check=True)
        subprocess.run([launcher,'--verify-only'],check=True)
        verified.append(launcher)
    for launcher in verified:
        os.replace(launcher,out/launcher.name)
        print('Verified',launcher.name,h.hex(),runtime.name)
