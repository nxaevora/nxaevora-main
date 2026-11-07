import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import re
# A strict contiguous LDR Wt / ADD(S) or SUB(S) #1 / STR Wd pattern.
# These are candidates, not all timers: counts and collection indices also match.
found=[]
for f,length in fns:
    for p in range(f,f+length-8,4):
        load,op,save=struct.unpack_from('<3I',b,p)
        if load&0xffc00000!=0xb9400000 or save&0xffc00000!=0xb9000000:continue
        if op&0x1f000000!=0x11000000 or op&0x80000000 or op&0x00400000:continue
        if ((op>>10)&0xfff)!=1 or ((op>>5)&31)!=(load&31) or op&31!=save&31:continue
        if (load&0x003fffe0)!=(save&0x003fffe0):continue
        if load&31==31 or op&31==31:continue
        names=vals(f)
        parents=[(c,vals(c)) for c in callers(f)] if not names else []
        hints=list(dict.fromkeys(names+[s for c,vs in parents for s in vs]))
        hits=[s for s in hints if re.search(r'frame|timer|wait|blink|animation|cool|duration|camera|effect',s,re.I)]
        found.append({'pc':p,'function':f,'size':length,'field':((load>>10)&0xfff)*4,
            'base':(load>>5)&31,'operation':'decrement' if op&0x40000000 else 'increment',
            'own_strings':names,'timing_hints':hits,'nearby_source':'direct callers' if not names else 'same function'})
Path('work/integer-timer-patterns.json').write_text(json.dumps(found,indent=2))
print('Strict read/modify/write candidates:',len(found),'in',len({r['function'] for r in found}),'functions')
hinted=[r for r in found if r['timing_hints']]
print('With own/direct-caller timing hints:',len(hinted))
for r in hinted:
    print(f"main+{r['pc']:X} owner={r['function']:X} size={r['size']:X} {r['operation']} X{r['base']}+{r['field']:X} {r['nearby_source']}: {r['timing_hints'][:12]}")
