# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Names the 64-bit words of a Maxwell shader program, and finds a word in the shader containers of an extracted RomFS.

    python tools/maxwell_label.py <file.arc> <hex word> [words of context]

The opcode names come from ScarletNX's maxwell_opcodes.def (the top 16 bits of a word, no operands). The file is read as a
Yaz0 or SARC container (nested ones too), and each place where the word is found is printed with the words around it.
Nothing is written to disk. Use it on the files of your own extracted RomFS. See docs/shader-analysis.md for what it found.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEF = os.path.join(HERE, "..", "third_party", "ScarletNX", "src", "nx", "gpu", "shader", "maxwell_opcodes.def")


def load_table():
    rows = []
    for line in open(DEF, encoding="utf-8"):
        m = re.match(r'^([A-Z0-9_]+)\s+"([^"]*)"\s+"([^"]*)"', line)
        if not m:
            continue
        pat = m.group(3).replace(" ", "")
        if len(pat) != 16:
            continue
        mask = int("".join("0" if c == "-" else "1" for c in pat), 2)
        val = int("".join("0" if c == "-" else c for c in pat), 2)
        rows.append((bin(mask).count("1"), m.group(1), mask, val))
    rows.sort(key=lambda r: -r[0])
    return rows


TABLE = load_table()


def name_of(word):
    top = word >> 48
    for _, name, mask, val in TABLE:
        if top & mask == val:
            return name
    return "?"


def yaz0(data):
    total = struct.unpack(">I", data[4:8])[0]
    out = bytearray()
    at = 16
    while len(out) < total and at < len(data):
        flags = data[at]
        at += 1
        for _ in range(8):
            if len(out) >= total or at >= len(data):
                break
            if flags & 0x80:
                out.append(data[at])
                at += 1
            else:
                b1, b2 = data[at], data[at + 1]
                at += 2
                dist = ((b1 & 0xF) << 8 | b2) + 1
                count = b1 >> 4
                if count == 0:
                    count = data[at] + 0x12
                    at += 1
                else:
                    count += 2
                for _ in range(count):
                    out.append(out[-dist])
            flags = (flags << 1) & 0xFF
    return bytes(out)


def sarc_entries(d):
    bom = struct.unpack("<H", d[6:8])[0]
    assert d[:4] == b"SARC" and bom == 0xFEFF
    sfat = struct.unpack("<H", d[4:6])[0]
    data0 = struct.unpack("<I", d[0xC:0x10])[0]
    count = struct.unpack("<H", d[sfat + 6:sfat + 8])[0]
    nodes = sfat + struct.unpack("<H", d[sfat + 4:sfat + 6])[0]
    sfnt = nodes + count * 16
    names = sfnt + struct.unpack("<H", d[sfnt + 4:sfnt + 6])[0]
    for i in range(count):
        n = nodes + i * 16
        attr, begin, end = struct.unpack("<III", d[n + 4:n + 16])
        name = ""
        if attr >> 24:
            at = names + (attr & 0xFFFF) * 4
            name = d[at:d.index(b"\0", at)].decode("latin1")
        yield name, data0 + begin, data0 + end


def flatten(blob, label, depth=0):
    """Yield (label, bytes) for the blob and every file in the SARC and Yaz0 layers in it."""
    if blob[:4] == b"Yaz0":
        blob = yaz0(blob)
    yield label, blob
    if blob[:4] == b"SARC" and depth < 4:
        for name, a, b in sarc_entries(blob):
            yield from flatten(blob[a:b], label + "/" + name, depth + 1)


if __name__ == "__main__":
    path, target = sys.argv[1], int(sys.argv[2], 16)
    ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    raw = open(path, "rb").read()
    needle = struct.pack("<Q", target)
    for label, blob in flatten(raw, path.split("/")[-1]):
        at = 0
        while True:
            at = blob.find(needle, at)
            if at < 0:
                break
            print("== %s: %d bytes, hit at 0x%x (8-aligned %s)" % (label, len(blob), at, at % 8 == 0))
            if at % 8 == 0:
                lo = max(0, (at // 8) - ctx) * 8
                hi = min(len(blob), (at // 8 + ctx + 1) * 8)
                for p in range(lo, hi, 8):
                    w = struct.unpack("<Q", blob[p:p + 8])[0]
                    mark = "<== target" if p == at else ""
                    print("  %06x %016x %-10s %s" % (p, w, name_of(w), mark))
            at += 8
