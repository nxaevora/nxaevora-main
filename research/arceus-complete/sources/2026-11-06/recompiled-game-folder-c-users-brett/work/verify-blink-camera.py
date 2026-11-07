from pathlib import Path
import struct
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
checks=[(0x194f110,0xf81e0ff3),(0x194f208,0x17ffffd5),(0x194f20c,0xa9be4ff4),(0x194f308,0x17ffffd5),(0x193c04c,0xfc1b0fec),(0x193c17c,0x1e222862),(0x193c2c0,0x17ffffee),(0x2482818,0xd10083ff),(0x24828cc,0x17ffffda)]
for off,expected in checks: assert struct.unpack_from('<I',b,off)[0]==expected,hex(off)
print('All new binary instruction guards match.')
# Timer conservation with changing frame intervals: no tick lost by truncating each frame.
for sequence in ([8333333]*1200,[16666667]*600,[8000000,12000000,20000000,100000000]*100):
 rem=total=0
 for ns in sequence:
  rem+=ns*30; ticks=rem//1000000000; rem%=1000000000; total+=ticks
 assert total==sum(sequence)*30//1000000000
 assert rem==sum(sequence)*30%1000000000
print('Blink tick conservation passed at 60/120 and variable FPS, including capped stalls.')
# Fractional camera steps preserve partial-stick equilibrium and linear full-stick acceleration.
def authored(x,a,k): return a*min(1,max(0,x+a*k)) if a else max(0,x-k)
def step(x,a,k,r):
 while r>0:
  y=authored(x,a,k); x=y if r>=1 else x+(y-x)*r; r-=1
 return x
for fps in (30,60,120):
 x=0
 for _ in range(fps//2):x=step(x,1,.02,30/fps)
 assert abs(x-.3)<1e-6,(fps,x)
 for _ in range(fps//2):x=step(x,0,.02,30/fps)
 assert x<.01,(fps,x)
 x=0
 for _ in range(fps*20):x=step(x,.5,.02,30/fps)
 assert abs(x-.01)<1e-6,(fps,x)
print('Camera full-stick ramp, release and partial-stick equilibrium checks passed.')
# Classify exact contiguous MOV/MOVK 1/30 constructions, rather than treating every use as a timer.
n=struct.unpack_from('<I',Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/exefs/main').read_bytes(),0x18)[0]
hits=[]
for off in range(0,n-4,4):
 w,v=struct.unpack_from('<II',b,off)
 if w&0xffffffe0==0x52911120 and v&0xffffffe0==0x72a7a100 and w&31==v&31:hits.append(off)
notes={0x29faf8c:'WaterCurrentTime increment: corrected and user-confirmed.',0x191d2b8:'Seconds-to-integer-frame conversion (FDIV then FCVTZS): leave unchanged.',0x1982930:'Paired physics conversion; nearby inverse approximately 30. Review pair before editing.',0x2795230:'Conditional fixed dt versus input S0: owner/update mode must be identified.',0x13b339c:'Timer +48c += 1/30; scene/type not established.',0x167ff0c:'Timer +190 += 1/30, capped; owner/update cadence not established.',0x16832c0:'Timer +1a0 += 1/30 with 2-second threshold; owner not established.',0x16832e4:'Timer +198 += 1/30 with 6-second threshold; owner not established.',0x16836d0:'Timer +19c += 1/30; owner not established.',0x198e020:'Vector increment multiplied by 1/30; physics owner/cadence not established.'}
out=['Arceus 1.1.1 fixed-step audit (all offsets relative to main)','',f'{len(hits)} exact contiguous float 1/30 MOV/MOVK sites found. This scan does not cover','literal-pool loads, nonadjacent constant construction, integer counters, or other rates.','No blanket replacements were applied.','']
for off in hits:out.append(f'main+{off:x}: '+notes.get(off,'Unclassified; keep unchanged until consumer and call cadence are verified.'))
out+=['','Additional verified frame-dependent paths:', 'main+194f110/+194f20c: Blink state machine; integer countdown -=1. New optional 30-Hz candidate.', 'main+193c04c: controller yaw/pitch acceleration/deceleration; per-call state change. New fractional-step candidate.', 'main+19b2140: event camera normalized progress += nominal increment. User-confirmed correction.', 'main+2b90040: effect elapsed time truncated to integer; fractional correction user-confirmed.', '', 'Dialogue advance: field manager main+2482818 -> main+27314e8 -> live message-tree update at vtable+80.', 'New read-only probe records actual live update targets. No dialogue/input behavior changed this round.']
Path('outputs/arceus-fixed-step-audit.txt').write_text('\n'.join(out)+'\n')
print(f'Wrote inventory of {len(hits)} constant sites.')
