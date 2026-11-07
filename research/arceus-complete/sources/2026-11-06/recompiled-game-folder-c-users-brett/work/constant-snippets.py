exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import subprocess
T=Path(r'C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin')
out=[]
for p in D['hits']:
 lo=p-16;hi=p+100
 Path('work/code.bin').write_bytes(b[lo:hi]);subprocess.run([str(T/'llvm-objcopy.exe'),'-I','binary','-O','elf64-littleaarch64','--rename-section','.data=.text,alloc,load,readonly,code','work/code.bin','work/code.elf'],capture_output=True,check=True)
 x=subprocess.run([str(T/'llvm-objdump.exe'),'-d','--no-show-raw-insn','--triple=aarch64',f'--adjust-vma={0x8005000+lo}','work/code.elf'],capture_output=True,text=True,check=True).stdout
 out.append(f'\nSITE {p:x}\n'+x)
Path('work/all-constant-snippets.txt').write_text('\n'.join(out))
