import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for a in [0x427A7B8,0x419A7F8,0x419A7F0,0x419A950,0x419A948]:
 print(hex(a), hex(D['rel'].get(a,0)),b[a:a+32].hex())
for f,z in fns:
 if 0x2CA0000<=f<0x2CC0000 and vals(f):print(hex(f),vals(f))
for a in [0x2cb6480,0x2cb8fcc,0x2F94354]:
 print(hex(a),'rev',[hex(v) for v in D['rev'].get(a,[])])
