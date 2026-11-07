"""Inspect local raw Vulkan attachments without modifying the source capture."""
import sys
sys.path.pop(0)
import argparse, csv, io, json, math, struct, zlib
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('manifest', type=Path)
p.add_argument('--index', type=int, action='append', help='Decode only these attachment indices')
p.add_argument('--normal-address', action='append', default=[],
               help='Confirmed packed-normal attachment guest address, hexadecimal')
args = p.parse_args()
manifest = args.manifest.resolve()
normal_addresses = {int(x, 16) for x in args.normal_address}
lines = manifest.read_text().splitlines()
rows = list(csv.DictReader(io.StringIO('\n'.join(x for x in lines if not x.startswith('#'))), delimiter='\t'))
out = Path('work/pass10-captures') / manifest.stem
out.mkdir(parents=True, exist_ok=True)

def byte(v):
    return 0 if math.isnan(v) or v <= 0 else 255 if v >= 1 else round(v * 255)

def png(path, w, h, rgb):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))

def packed_ufloat(v, mantissa_bits):
    m = v & ((1 << mantissa_bits) - 1)
    e = (v >> mantissa_bits) & 31
    if e == 31:
        return math.nan
    return math.ldexp(m, -14-mantissa_bits) if e == 0 else math.ldexp(1 + m / (1 << mantissa_bits), e-15)

def values(data, fmt):
    layouts = {9:'B', 13:'B', 37:'4B', 43:'4B', 70:'H', 77:'2H', 78:'2h',
               76:'e', 97:'4e', 100:'f', 103:'2f', 109:'4f', 124:'H', 128:'H',
               125:'I', 129:'I', 126:'f', 130:'f', 122:'I'}
    for v in struct.iter_unpack('<' + layouts[fmt], data):
        if fmt in (9, 13, 37, 43):
            yield tuple(x / 255 for x in v)
        elif fmt in (70, 77, 124, 128):
            yield tuple(x / 65535 for x in v)
        elif fmt == 78:
            yield tuple(max(x / 32767, -1) * .5 + .5 for x in v)
        elif fmt in (125, 129):
            yield ((v[0] & 0xffffff) / 0xffffff,)
        elif fmt == 122:
            a = v[0]
            yield (packed_ufloat(a & 2047, 6), packed_ufloat((a >> 11) & 2047, 6), packed_ufloat((a >> 22) & 1023, 5))
        else:
            yield v

formats = {9,13,37,43,70,77,78,76,97,100,103,109,124,125,126,128,129,130,122}

summary = []
for row in rows:
    if args.index is not None and int(row['index']) not in args.index:
        continue
    if row['status'] != 'ok':
        summary.append(dict(index=row['index'], status=row['status']))
        continue
    raw = Path(row['file'])
    if not raw.is_absolute():
        raw = manifest.parent / raw.name
    if raw.resolve().parent != manifest.parent:
        raise ValueError(f'Attachment is outside its capture directory: {raw}')
    data = raw.read_bytes()
    w, h, layers, bpp, fmt = [int(row[k]) for k in ('image_w', 'image_h', 'layers', 'bpp', 'vkformat')]
    if len(data) != int(row['bytes']) or len(data) != w * h * layers * bpp:
        raise ValueError(f'Capture length mismatch: {raw}')
    entry = dict(row, interpretation='Raw stored channels, normalized integers or clipped floats; no gamma conversion', previews=[])
    if fmt in formats:
        low, high = math.inf, -math.inf
        for layer in range(layers):
            layer_data = data[layer*w*h*bpp:(layer+1)*w*h*bpp]
            rgb = bytearray()
            for v in values(layer_data, fmt):
                for x in v:
                    if math.isfinite(x):
                        low, high = min(low, x), max(high, x)
                v = v * 3 if len(v) == 1 else v + (0,) if len(v) == 2 else v
                rgb.extend(byte(x) for x in v[:3])
            name = f"{int(row['index']):02}_{row['gva']}_layer{layer}.png"
            png(out/name, w, h, rgb)
            entry['previews'].append(name)
        entry['range'] = [low, high] if low <= high else None
        if row['aspect'] == 'depth' and low < high:
            for layer in range(layers):
                rgb = bytearray()
                for v in values(data[layer*w*h*bpp:(layer+1)*w*h*bpp], fmt):
                    rgb.extend([byte((v[0]-low)/(high-low))] * 3)
                name = f"{int(row['index']):02}_{row['gva']}_layer{layer}-range.png"
                png(out/name, w, h, rgb)
    if int(row['gva'], 16) in normal_addresses:
        if fmt != 37 or bpp != 4:
            raise ValueError('Packed-normal decoding requires confirmed RGBA8_UNORM storage')
        for layer in range(layers):
            rgb = bytearray()
            for r,g,b,_ in struct.iter_unpack('4B', data[layer*w*h*4:(layer+1)*w*h*4]):
                x, y = ((r << 4) | (g >> 4))/2047-1, (((g & 15) << 8) | b)/2047-1
                z = 1-abs(x)-abs(y)
                if z < 0:
                    x,y = (1-abs(y))*(-1 if x < 0 else 1), (1-abs(x))*(-1 if y < 0 else 1)
                length = max(math.sqrt(x*x+y*y+z*z), 1e-20)
                rgb.extend(byte(v/length*.5+.5) for v in (x,y,z))
            name = f"{int(row['index']):02}_{row['gva']}_layer{layer}-decoded-normal.png"
            png(out/name, w, h, rgb)
            entry['previews'].append(name)
        entry['normal_decode'] = 'Confirmed-address 12-bit octahedral normal; background may not contain valid normals'
    summary.append(entry)
(out/'summary.json').write_text(json.dumps(dict(source=str(manifest), metadata=[x for x in lines if x.startswith('#')],
    scope='Raw attachment previews are in guest orientation. Use paired frame metadata for crop/transform. Depth range previews are illustrative.',
    attachments=summary), indent=2))
print(f'Validated {len(rows)} records; wrote local previews to {out}')
