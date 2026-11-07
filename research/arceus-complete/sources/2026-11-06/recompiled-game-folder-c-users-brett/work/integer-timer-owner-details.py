import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for f in [0x180c338,0x2549fa0,0x24ee204,0x1458300,0x19c2f80,0x1b5bbb8]:
 print(hex(f), 'callers',[(hex(c),vals(c)[:8]) for c in callers(f)])
 print('clock clues',[(hex(p),s) for p,t,s in D['srefs'].get(f,[]) if 'frame' in s.lower() or 'wait' in s.lower()][:16])
