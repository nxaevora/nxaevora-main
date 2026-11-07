"""Evaluate the emitted coordinate arithmetic, not a parallel copy of the fix.
Restricted straight-line SPIR-V evaluator: unknown operations fail when reached.
It does not validate a full shader module or execute image sampling/control flow.
"""
import sys
sys.path.pop(0)
from pathlib import Path
import struct, math, functools, json

def f32(x): return struct.unpack('<f',struct.pack('<f',x))[0]
def bits(x): return struct.unpack('<I',struct.pack('<f',x))[0]
def floating(x): return struct.unpack('<f',struct.pack('<I',x&0xffffffff))[0]

class Shader:
    def __init__(self,path):
        blob=Path(path).read_bytes(); w=struct.unpack('<'+'I'*(len(blob)//4),blob)
        assert w[:1]==(0x07230203,)
        self.nodes=[]; self.types={}; self.decor={}; self.variables={}; self.fetch=[]
        defs={}; memory={}; at=5
        def node(t,op,args):
            self.nodes.append((t,op,args)); return len(self.nodes)-1
        while at<len(w):
            count=w[at]>>16; op=w[at]&65535; a=w[at+1:at+count]
            assert count and at+count<=len(w); at+=count
            if op==71: self.decor.setdefault(a[0],{})[a[1]]=a[2:]; continue
            if 19<=op<=33: self.types[a[0]]=(op,a[1:]); continue
            if op==59:
                self.variables[a[1]]=(a[0],a[2]); defs[a[1]]=node(a[0],'var',(a[1],))
                if len(a)>3:memory[a[1]]=defs[a[3]]
                continue
            if op==62:memory[a[0]]=defs[a[1]];continue
            if op==61:
                defs[a[1]]=memory.get(a[2]) if a[2] in memory else node(a[0],'load',(defs[a[2]],));continue
            if op in (41,42,43):
                val=op==41 if op in (41,42) else floating(a[2]) if self.types[a[0]][0]==22 else a[2]
                defs[a[1]]=node(a[0],'constant',(val,));continue
            if op in (65,66):defs[a[1]]=node(a[0],'access',tuple(defs[x] for x in a[2:]));continue
            if op==81:defs[a[1]]=node(a[0],'extract',(defs[a[2]],*a[3:]));continue
            if op==12:defs[a[1]]=node(a[0],'glsl',(a[3],*(defs[x] for x in a[4:])));continue
            if op in (54,55,56,248,249,250,251,252,253,254,255,246,247,71,72,15,16,17,14,11,10,5,6,3):continue
            if len(a)>=2:
                # Image operations have literal operands; only their coordinate is evaluated.
                if op==87:self.fetch.append(defs[a[3]])
                defs[a[1]]=node(a[0],op,tuple(defs.get(x,('unknown',x)) for x in a[2:]))
        assert at==len(w)

    def coordinates(self,x,y,scale,mask,fetch=0,w=1.,uv=(0.,0.),transform=(1.,1.,0.,0.)):
        @functools.cache
        def e(i):
            if not isinstance(i,int):raise ValueError(f'unresolved operand {i}')
            t,op,a=self.nodes[i]
            if op=='constant':return a[0]
            if op=='var':return (a[0],)
            if op=='access':return (*e(a[0]),*(e(j) for j in a[1:]))
            if op=='load':
                ptr=e(a[0]); var=ptr[0]; dec=self.decor.get(var,{})
                if dec.get(11)==(15,):return (f32(x),f32(y),0.0,f32(w))
                if dec.get(33)==(106,):
                    assert ptr[1:3]==(0,0); return (f32(1/scale),0.,f32(scale),0.)[ptr[3]]
                if self.variables[var][1]==9:
                    assert ptr[1:]==(1,0);return mask
                if dec.get(30)==(0,):return (f32(uv[0]),f32(uv[1]),0.,1.)
                if dec.get(33)==(8,):
                    assert ptr[1:3]==(0,1),ptr
                    return bits(f32(transform[ptr[3]]))
                raise ValueError(f'unhandled input {ptr} {dec}')
            if op=='extract':
                v=e(a[0])
                for j in a[1:]:v=v[j]
                return v
            if op==169:return e(a[1] if e(a[0]) else a[2])
            if op=='glsl':
                vs=[e(j) for j in a[1:]]
                if a[0]==3:return float(math.trunc(vs[0]))
                if a[0]==8:return float(math.floor(vs[0]))
                if a[0]==43:return max(vs[1],min(vs[2],vs[0]))
                if a[0]==50:return f32(math.fma(*vs))
                raise ValueError(f'unsupported GLSL {a[0]}')
            v=[e(j) for j in a]
            if op==124:
                src=self.types[self.nodes[a[0]][0]][0];dst=self.types[t]
                raw=bits(v[0]) if src==22 else v[0]&0xffffffff
                if dst[0]==22:return floating(raw)
                return raw-0x100000000 if dst[0]==21 and dst[1][1] and raw&0x80000000 else raw
            if op==80:return tuple(v)
            if op in (109,110):return math.trunc(v[0])
            if op in (111,112):return f32(v[0])
            if op==127:return f32(-v[0])
            if op==129:return f32(v[0]+v[1])
            if op==131:return f32(v[0]-v[1])
            if op==136:return f32(v[0]/v[1])
            if op==133:return f32(v[0]*v[1])
            if op==156:return math.isnan(v[0])
            if op==171:return v[0]!=v[1]
            if op==190:return v[0]>=v[1]
            if op==194:return (v[0]&0xffffffff)>>v[1]
            if op==199:return v[0]&v[1]
            raise ValueError(f'unsupported op {op}')
        return e(self.fetch[fetch])

