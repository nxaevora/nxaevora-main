from pathlib import Path
import struct,re,sys
header=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/common/utilities/spirv_emit.h').read_text()
names={int(v,0):n for n,v in re.findall(r'SPV_OP_(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)',header)}
for file in sys.argv[1:]:
    p=Path(file);w=struct.unpack('<'+'I'*(p.stat().st_size//4),p.read_bytes());ops=[];at=5
    while at<len(w):
        n=w[at]>>16;ops.append((w[at]&65535,w[at+1:at+n]));at+=n
    defs={};stored={};decor={};types={};fetch=[]
    def expr(i):return defs.get(i,('%'+str(i),))
    for op,a in ops:
        if op==71:decor.setdefault(a[0],[]).append(a[1:])
        if op in [19,20,21,22,23,24,25,26,27,28,29,30,32,33]:types[a[0]]=(op,a[1:]);continue
        if op==43:
            t=types.get(a[0]);v=a[2]
            if t and t[0]==22 and t[1][0]==32:v=struct.unpack('<f',struct.pack('<I',v))[0]
            defs[a[1]]=(str(v),);continue
        if op==59:defs[a[1]]=('var'+str(a[1])+str(decor.get(a[1],[])),);continue
        if op==62:stored[a[0]]=expr(a[1]);continue
        if op==61:defs[a[1]]=stored.get(a[2],('load',expr(a[2])));continue
        if op in (65,66):defs[a[1]]=('access',*(expr(i) for i in a[2:]));continue
        if op==81:defs[a[1]]=('extract'+str(a[3:]),expr(a[2]));continue
        if op==12:defs[a[1]]=('glsl'+str(a[3]),*(expr(i) for i in a[4:]));continue
        if op in (54,55,56,248,249,250,251,252,253,254,255,246,247,71,72,15,16,17,14,11,5,6,3):continue
        if len(a)>=2:
            if op==95:
                fetch.append((a[1],expr(a[2]),expr(a[3])))
            defs[a[1]]=(names.get(op,'op'+str(op)),*(expr(i) for i in a[2:]))
    def render(e,depth=0):
        if len(e)==1:return e[0]
        if e[0]=='BITCAST':return render(e[1],depth)
        if e[0]=='SELECT' and e[1][0]=='IS_NAN':return 'safe_int('+render(e[1][1],depth+1)+')'
        if depth>14:return e[0]+'(...)'
        return e[0]+'('+','.join(render(x,depth+1) for x in e[1:])+')'
    print('\nSHADER',p.stem)
    for n,img,c in fetch:print('FETCH',n,'IMAGE',render(img),'COORD',render(c))
