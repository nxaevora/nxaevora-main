import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import subprocess,re
root=Path('work/pass6-events');root.mkdir(exist_ok=True)
bindir=Path(r'C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin')
for arg in sys.argv[1:]:
    f=owner(int(arg,16))
    if f is None:raise ValueError(arg)
    raw=root/f'{f:x}.bin';elf=raw.with_suffix('.elf');out=raw.with_suffix('.txt')
    raw.write_bytes(b[f:f+size[f]])
    subprocess.run([str(bindir/'llvm-objcopy.exe'),'-I','binary','-O','elf64-littleaarch64','--rename-section','.data=.text,alloc,load,readonly,code',str(raw),str(elf)],check=True)
    asm=subprocess.check_output([str(bindir/'llvm-objdump.exe'),'-d','--no-show-raw-insn','--triple=aarch64',f'--adjust-vma={0x8005000+f}',str(elf)],text=True)
    notes={p:s for p,t,s in D['srefs'].get(f,[])}
    lines=[f'main+{f:X}, length {size[f]:X}',f'Direct callers: {[hex(c) for c in callers(f)]}',f'Strings: {vals(f)}','']
    for line in asm.splitlines():
        m=re.match(r'\s*([0-9a-f]+):\s+(.*)',line)
        if not m:continue
        p=int(m[1],16)-0x8005000;ins=m[2].split(' <')[0]
        ins=re.sub(r'(?<!#)0x([0-9a-f]{7,})',lambda m:f'main+{int(m[1],16)-0x8005000:X}' if int(m[1],16)>=0x8005000 else m[0],ins)
        annotation=' ; STRING '+repr(notes[p]) if p in notes else ''
        lines.append(f'{p:08X}: {ins}{annotation}')
    out.write_text('\n'.join(lines)+'\n')
    print(out,'length',hex(size[f]),'callers',len(callers(f)))

