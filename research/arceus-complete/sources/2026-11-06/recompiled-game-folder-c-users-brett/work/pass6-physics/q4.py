import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for a in [0x2FECB8C,0x2FF50D0,0x2AD30B8,0x2AE27B8,0x2F4B100,0x2F4CA30,0x2F4F924,0x2F51F1C,0x2F9BB78]:
 print(hex(a),hex(owner(a)), 'strings',vals(owner(a)),'rev',[hex(v) for v in D['rev'].get(a,[])])
for a in [0x2AD30B8,0x2AE27B8,0x2CD6D6C]:
 f=owner(a)
 print('directrefs',hex(a),[(hex(x),hex(owner(x))) for x in D['refs'].get(a,[])])
