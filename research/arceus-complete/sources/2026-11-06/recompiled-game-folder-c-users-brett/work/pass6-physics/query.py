import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for a in [0x2CD0CE0,0x2CD11EC,0x2CD121C,0x2CD6D6C,0x2F94354,0x2F94000,0x2AD30B8,0x2AE27B8]:
 print(hex(a),'owner',hex(owner(a)), 'calls', [hex(v) for v in callers(owner(a))], 'refs', [hex(v) for v in D['refs'].get(a,[])], 'rev',[hex(v) for v in D['rev'].get(a,[])], 'strings', vals(owner(a)))
print('STRINGS 2CC..2CE')
for f in fns:
 if 0x2CC0000<=f[0]<0x2CE0000 and vals(f[0]):print(hex(f[0]),vals(f[0]))
