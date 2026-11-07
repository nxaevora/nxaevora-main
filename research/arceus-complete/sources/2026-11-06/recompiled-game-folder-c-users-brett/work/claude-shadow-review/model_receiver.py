"""Offline model of ha_receive_shadow (e9e7255e7594a006) on planar receivers.
Decoded receiver semantics (guest listing pass8-shader/e9e7255e7594a006.txt):
  ref = z_shadow + k*tan(theta)          (slope-scaled bias, k per material from cb)
  16 taps: TEX of D32 array, LINEAR filter, no compare; FSET ref<=sample; mean*1/16
  tap offsets = c_offset * (radius / TXQ size)  -> texel units of the SAMPLED array
Model units: depth gradient g per DEST texel, bias = beta*g (beta = k/T in texels,
independent of theta).  The 4x4 offset grid stands in for the cb1 table (unknown).
Methods for producing the 1024 array from a 4/3-scaled 1365 raster (viewport 1365.333):
  native   : rasterized at 1024 directly
  nearest  : current copy_layers, vkCmdBlit NEAREST 1365->1024
  linexact : candidate, LINEAR blit of 1364->1023 region (exact 4/3) + last column
"""
import numpy as np
N, S, V = 1024, 1365, 1024*4/3

def raster_src(plane):            # 1365 raster, centres in viewport space
    j = np.arange(S) + 0.5
    u = j * N / V                 # dest-texel units
    return plane(u[None, :], u[:, None])

def bilinear(img, x, y):          # Vulkan linear, texel-centre convention, clamp-to-edge
    h, w = img.shape
    x = x - 0.5; y = y - 0.5
    x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int)
    fx = x - x0; fy = y - y0
    def at(yy, xx): return img[np.clip(yy, 0, h-1), np.clip(xx, 0, w-1)]
    return ((1-fx)*(1-fy)*at(y0, x0) + fx*(1-fy)*at(y0, x0+1)
            + (1-fx)*fy*at(y0+1, x0) + fx*fy*at(y0+1, x0+1))

def make_array(method, plane):
    i = np.arange(N) + 0.5
    if method == 'native':
        return plane(i[None, :], i[:, None])
    src = raster_src(plane)
    if method == 'nearest':
        s = np.minimum(np.floor(i * S / N).astype(int), S-1)
        return src[np.ix_(s, s)]
    if method == 'linexact':      # blit src[0,1364) -> dst[0,1023): ratio exactly 4/3
        sc = i * (1364/1023)
        sc[-1] = 1364.5           # dst column 1023 from blit src[1364,1365)->dst[1023,1024)
        X, Y = np.meshgrid(sc, sc)
        return bilinear(src, X, Y)
    raise ValueError(method)

OFFS = np.array([-1.5, -0.5, 0.5, 1.5])
def visibility(arr, plane, px, py, beta, g, radius=1.0):
    ref = plane(px, py) - beta * g   # bias moves the receiver toward the light
    vis = np.zeros_like(px)
    for oy in OFFS:
        for ox in OFFS:
            d = bilinear(arr, px + ox*radius, py + oy*radius)
            vis += (ref <= d)
    return vis / 16

rng = np.random.default_rng(1)
pts = rng.uniform(40, N-40, size=(2, 200000))
print('False shadowing on planar receivers (fraction of receiver points with vis<1, mean darkening)')
print('beta = bias in shadow texels of slope; g = depth gradient per texel')
for beta in [0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.75, 1.0]:
    row = [f'beta {beta:4.2f}']
    for method in ['native', 'nearest', 'linexact']:
        acc = []; dark = []
        for trial in range(6):
            ang = rng.uniform(0, 2*np.pi); g = 1e-4
            gx, gy = g*np.cos(ang), g*np.sin(ang)
            plane = lambda u, v, gx=gx, gy=gy: 0.5 + gx*u + gy*v
            arr = make_array(method, plane)
            vis = visibility(arr, plane, pts[0], pts[1], beta, g)
            acc.append((vis < 1).mean()); dark.append((1-vis).mean())
        row.append(f'{method}: {np.mean(acc)*100:5.1f}% / {np.mean(dark):.4f}')
    print('  '.join(row))

# periodicity of the nearest error: residual plane error per dest column phase
plane = lambda u, v: 0.5 + 1e-4*u
for method in ['nearest', 'linexact']:
    arr = make_array(method, plane); i = np.arange(N)+0.5
    err = (arr[512] - plane(i, 0)) / 1e-4
    print(method, 'stored-texel position error (texels) by column mod 3:',
          [round(float(err[100+p::3][:200].mean()), 4) for p in range(3)], 'max', round(float(np.abs(err[40:-40]).max()), 4))

# silhouette: caster edge at u=500.3 (near depth 0.3 left, receiver depth 0.6 right)
def edge(u, v): return np.where(u < 500.3, 0.3, 0.6) + 0*v
print('Silhouette: shadow-edge position of a step caster at u=500.3 (vis=0.5 crossing, row 512)')
for method in ['native', 'nearest', 'linexact']:
    arr = make_array(method, edge)
    xs = np.linspace(490, 512, 4401); ys = np.full_like(xs, 512.3)
    flat = lambda u, v: 0.6 + 0*u
    vis = visibility(arr, flat, xs, ys, 0.5, 1e-5)
    k = np.argmax(vis >= 0.5); print(f'  {method:8s} edge at u={xs[k]:.3f}')
