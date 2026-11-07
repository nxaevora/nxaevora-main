exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for arg in sys.argv[1:]:
 p=int(arg,16)
 print('\nREGION',hex(p))
 for f,ss in D['srefs'].items():
  if p-0x8000<=f<=p+0x8000:print(hex(f),str(vals(f))[:2000])
