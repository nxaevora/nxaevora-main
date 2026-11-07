import sys
sys.path.pop(0)
from pathlib import Path
import struct, collections, json
p = Path('work/pass9-texture')
results = {}
for kind in range(7):
    blocks = (p / f'native-{kind}-blocks.bin').read_bytes()
    got = struct.unpack('<16384f', (p / f'native-{kind}-got.bin').read_bytes())
    buckets = collections.defaultdict(list)
    for i in range(4096):
        x, y = i % 64, i // 64
        bi, pix = (y // 4) * 16 + x // 4, (y % 4) * 4 + x % 4
        if kind < 3:
            bb = 8 if kind == 0 else 16
            d = int.from_bytes(blocks[(bi+1)*bb-8:(bi+1)*bb], 'little')
            ends = [d & 65535, (d >> 16) & 65535]
            ix = (d >> (32+2*pix)) & 3
            mode = 'four' if ends[0] > ends[1] or kind > 0 else 'three'
            for c, (shift, mask, bits) in enumerate(((11, 31, 5), (5, 63, 6), (0, 31, 5))):
                raw = [(e >> shift) & mask for e in ends]
                ex = [(e << (8-bits)) | (e >> (2*bits-8)) for e in raw]
                if ix < 2:
                    buckets[f'endpoint channel{c}'].append(abs(got[4*i+c]*255 - ex[ix]))
                elif ix == 3 and mode == 'three':
                    buckets[f'transparent channel{c}'].append(abs(got[4*i+c]*255))
                elif abs(ex[1]-ex[0]) > 100:
                    buckets[f'{mode} channel{c} index{ix} weight'].append((got[4*i+c]*255-ex[0])/(ex[1]-ex[0]))
        else:
            nc, sn = (1 if kind < 5 else 2), kind in (4, 6)
            bb = nc*8
            for c in range(nc):
                d = int.from_bytes(blocks[bi*bb+c*8:bi*bb+c*8+8], 'little')
                e0, e1, ix = d & 255, (d >> 8) & 255, (d >> (16+3*pix)) & 7
                if sn:
                    e0 = max(-127, e0 if e0 < 128 else e0-256)
                    e1 = max(-127, e1 if e1 < 128 else e1-256)
                out = got[4*i+c]*(127 if sn else 255)
                if ix < 2:
                    buckets[f'endpoint index{ix} delta'].append(out-(e0 if ix == 0 else e1))
                if abs(e1-e0) > 100:
                    buckets[f'{"eight" if e0 > e1 else "six"} index{ix} weight'].append((out-e0)/(e1-e0))
    results[str(kind)] = {k: {'n':len(v), 'min':min(v), 'max':max(v), 'mean':sum(v)/len(v)} for k,v in sorted(buckets.items())}
(p / 'native-interpolant-analysis.json').write_text(json.dumps(results, indent=2))
for k,r in results.items():
    print('KIND', k)
    for label, v in r.items():
        print(label, v['n'], 'min=%.8f max=%.8f mean=%.8f' % (v['min'],v['max'],v['mean']))
