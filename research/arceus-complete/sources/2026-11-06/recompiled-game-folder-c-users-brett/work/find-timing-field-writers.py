import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
targets={0x121:[],0x82:[],0x320:[]}
for p in range(0,max(a+z for a,z in fns),4):
    w=struct.unpack_from('<I',b,p)[0]
    if w&0xffc00000==0x39000000:
        field=(w>>10)&0xfff
        if field in (0x121,0x82):targets[field].append(p)
    if w&0xffc00000==0xb9000000 and ((w>>10)&0xfff)*4==0x320:
        targets[0x320].append(p)
for field,pcs in targets.items():
    print('\nFIELD',hex(field),'total',len(pcs))
    for p in pcs:
        f=owner(p)
        if f is None:continue
        if field==0x82 and not 0x1040000<=f<0x1060000:continue
        if field==0x320 and not 0x1b00000<=f<0x1d50000:continue
        print('site',hex(p),'owner',hex(f),'size',hex(size[f]),'words',[hex(struct.unpack_from('<I',b,q)[0]) for q in range(p-8,p+4,4)])
        print(' strings',vals(f))
        print(' callers',[(hex(c),vals(c)[:8]) for c in callers(f)[:8]])
