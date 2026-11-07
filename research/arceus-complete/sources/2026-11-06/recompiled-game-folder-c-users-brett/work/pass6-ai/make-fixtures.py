import sys
sys.path.pop(0)
from pathlib import Path
import struct,json,hashlib
root=Path('work/pass6-ai');dump=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625')
b=(dump/'main.bin').read_bytes();sdk=(dump/'sdk.bin').read_bytes()
groups=[('initkey',0xb32a78,0xb32c88,{0xb32c84:0xd65f03c0}),('initturn',0xb335ec,0xb337ec,{0xb33760:0xd65f03c0}),('motionkey',0xb32e34,0xb32fc0,{0xb32fbc:0xd65f03c0}),('motionturn',0xb33ad4,0xb33c60,{0xb33c5c:0xd65f03c0}),('countkey',0xb32e00,0xb32e10,{0xb32e0c:0xd65f03c0}),('countturn',0xb33aa0,0xb33ab0,{0xb33aac:0xd65f03c0}),('endkey',0xb32fc0,0xb32fe8,{0xb32fe4:0xd61f0380}),('endturn',0xb33c60,0xb33cac,{0xb33c7c:0xd61f0380}),('direction',0x19853a8,0x19856a8,{}),('success',0xbd4380,0xbd438c,{}),('running',0xbd43a4,0xbd43b0,{}),('acosplt',0x32a30b0,0x32a30c0,{})]
lines=['/* Local extracted instructions and constants; keep local. */'];manifest=[]
for name,start,end,changes in groups:
 raw=bytearray(b[start:end])
 for p,w in changes.items():struct.pack_into('<I',raw,p-start,w)
 lines.append('static const u8 '+name+'_code[]={'+','.join(str(x) for x in raw)+'};')
 manifest.append({'name':name,'module':'main','start':hex(start),'end_exclusive':hex(end),'substitutions':[{'address':hex(p),'original':hex(struct.unpack_from('<I',b,p)[0]),'replacement':hex(w),'reason':'Return at slice continuation; BR X28 uses zero sentinel after helper BL' if w==0xd61f0380 else 'Return at extracted continuation'} for p,w in changes.items()]})
lines.append('static const u8 acos_code[]={'+','.join(str(x) for x in sdk[0x5295f0:0x529870])+'};')
lines.append('static const u8 turn_constants[]={'+','.join(str(x) for x in b[0x3788000:0x378b000])+'};')
syms=json.loads(root.joinpath('sdk-symbols.json').read_text())['symbols']
gotnames={0x4272ce0:'FloatDegree180',0x4272cf0:'FloatPiE',0x4272d20:'Float1Divided2Pi',0x4272d28:'SinCoefficients',0x4272d30:'Float2Pi',0x4272d38:'FloatPiDivided2',0x4272d40:'CosCoefficients',0x427a118:'FloatQuaternionEpsilon'}
consts=[]
for i,(got,part) in enumerate(gotnames.items()):
 name=next(n for n in syms if part in n);s=syms[name];off=int(s['offset'],16);z=s['length'];addr=0x4208000+i*0x40
 lines.append('static const u8 const'+str(i)+'[]={'+','.join(str(x) for x in sdk[off:off+z])+'};')
 consts.append({'got':hex(got),'symbol':name,'sdk_offset':hex(off),'size':z,'synthetic_address':hex(addr)})
lines.append('static void install_constants(void){')
for i,c in enumerate(consts):lines.append('memcpy(memory+'+c['synthetic_address']+',const'+str(i)+',sizeof const'+str(i)+');put64('+c['got']+','+c['synthetic_address']+');')
lines.append('put64(0x4270f20,0x5295f0);}')
root.joinpath('turn-fixtures.h').write_text('\n'.join(lines)+'\n')
manifest.append({'name':'acos','module':'sdk','start':'0x5295f0','end_exclusive':'0x529870','substitutions':[]})
root.joinpath('fixture-provenance.json').write_text(json.dumps({'main_sha256':hashlib.sha256(b).hexdigest(),'sdk_sha256':hashlib.sha256(sdk).hexdigest(),'ranges':manifest,'main_constants':['0x3788000','0x378b000'],'sdk_constants':consts,'status_globals':{'0x40e7bf8':1,'0x40e7c04':4,'verified_original':True},'stubs':{'0x8c69bc':'Controlled actor-component acquire: writes COMPONENT into [X8]. Reference +58 points to REF whose +C is reset to2 before setter call; original release then decrements to1, never destroys.','0x0':'Recognized extraction stop marker for completion slices BR X28 with X28=0; not a game function.'},'addresses':'All main instruction addresses retained. sdk acos runs at its sdk-relative address in otherwise unused synthetic memory; it uses no PC-relative data. Its fallback sqrt helper is unprovided and any execution is an unexpected failure. Constants from sdk are copied to synthetic addresses and original main GOT slots point there.','scope':'Original init arithmetic (not entry resources/config lookup); separate original count/motion/completion slices; full direction setter with real SDK acos and controlled component acquisition; host proposed fractional progress simulation is explicitly separate from original code. SIM/SMALL/HITCH/RESIDUAL schedules and float progress are host-controlled models; original full action acquisition and scheduler are not executed.'},indent=2)+'\n')

