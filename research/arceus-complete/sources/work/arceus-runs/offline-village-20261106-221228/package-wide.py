from pathlib import Path
import hashlib,re,shutil,subprocess
base=Path(__file__).resolve().parent
project=Path(r'C:\Users\brett\Documents\Codex\2026-11-06\fix')
out=project/'outputs'; private=out/'village-v2-runtime'; private.mkdir(exist_ok=True)
runtime=private/'ArceusTest.exe'
shutil.copy2(base/'wide/ArceusTest.exe',runtime)
shutil.copy2(base/'wide/writeback.patch',private/'writeback.patch')
source=(project/'work/rim-candidate/controller-rim.c').read_text()
source=source.replace('rim-runtime','village-v2-runtime').replace('current-rim-test','current-village-v2-test').replace('Arceus neutral-rim candidate','Arceus village CPU build').replace('Neutral-rim workaround candidate','Village writeback optimization').replace(r'\"isolated_runtime\":true',r'\"village_writeback_v2\":true,\"isolated_runtime\":true')
digest=','.join(f'0x{x:02x}' for x in hashlib.sha256(runtime.read_bytes()).digest())
source,n=re.subn(r'(static int diagnostic_ok\(void\)\{static const unsigned char h\[32\]=\{)[^}]+',lambda m:m[1]+digest,source)
assert n==1
c=base/'wide/controller-village.c'; c.write_text(source)
exe=out/'Arceus-Village-CPU-v2.exe'
cc=r'C:\Users\brett\Documents\Codex\work\llvm-mingw-20261104-ucrt-x86_64\bin\clang.exe'
subprocess.run([cc,'-DSHADOWMODE=1','-municode','-mwindows','-O2',str(c),'-o',str(exe),'-lbcrypt','-luser32'],check=True)
subprocess.run([str(exe),'--verify-only'],check=True)
subprocess.run([str(base/'wide/test-release.exe')],check=True)
print('Verified launcher, installed runtime digest and writeback regression.')

