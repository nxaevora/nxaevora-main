#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Writes docs/nvn-coverage.md: one row for each NVN function that the game resolves (step 4-u1 of docs/nvn-design.md).

    python tools/nvn_coverage.py [--log RUN.log] [--sdk game/work/Arceus/exefs/sdk] [--check]

The names and addresses come from, in this order: the `nvn: <name> -> <address>` lines of a log with nvn_trace=1
(--log), the name table of the NVN library in the sdk module of the ExeFS (--sdk), or the rows of the coverage file that
exists. The calls come from the `nvn-used:` lines of the log, else from docs/nvn-calls.md. The handler state comes from
the table native_procs of nvn_native.c. The columns checks, seen and notes are written by hand and kept.
With --check nothing is written, and the exit code is 1 when the file would change. Nothing of the game is written.
"""

import argparse
import os
import re
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
COVERAGE = os.path.join(ROOT, 'docs', 'nvn-coverage.md')
CALLS = os.path.join(ROOT, 'docs', 'nvn-calls.md')
NATIVE = os.path.join(ROOT, 'third_party', 'ScarletNX', 'src', 'nx', 'gpu', 'nvn_native.c')
SDK_BASE = 0xd3c7000  # the address of the sdk module in the update program (the addresses that docs/ quote)
MANUAL = ('checks', 'seen', 'notes')
COLUMNS = ('function', 'group', 'address', 'calls', 'handler') + MANUAL

# --- the sdk module: NSO segments, relocations and the library's table of names and functions ---

NSO_MAGIC = b'NSO0'
NSO_SEGMENTS = 3
R_AARCH64_RELATIVE = 1027
DT_RELA, DT_RELASZ = 7, 8
RET = 0xd65f03c0
ANCHOR = b'\0DeviceInitialize\0'  # a name that every NVN version has, inside the sorted name blob
MIN_NAMES = 100


def lz4_block(src, size):
    """The bytes of one LZ4 block (the format of the NSO segments)"""
    out = bytearray()
    i = 0
    while i < len(src):
        token = src[i]
        i += 1
        literal = token >> 4
        if literal == 15:
            while True:
                literal += src[i]
                i += 1
                if src[i - 1] != 255:
                    break
        out += src[i:i + literal]
        i += literal
        if i >= len(src):
            break
        offset = src[i] | (src[i + 1] << 8)
        i += 2
        match = token & 15
        if match == 15:
            while True:
                match += src[i]
                i += 1
                if src[i - 1] != 255:
                    break
        start = len(out) - offset
        for k in range(match + 4):
            out.append(out[start + k])
    if len(out) != size:
        raise SystemExit('a segment of the sdk module does not decompress to its size')
    return bytes(out)


def nso_image(path):
    """(image, text size): the segments of an NSO at their memory offsets, without the BSS"""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != NSO_MAGIC:
        raise SystemExit('%s is not an NSO' % path)
    flags = struct.unpack_from('<I', data, 0xC)[0]
    file_sizes = struct.unpack_from('<3I', data, 0x60)
    segments = []
    for k in range(NSO_SEGMENTS):
        file_offset, memory_offset, size = struct.unpack_from('<3I', data, 0x10 + 0x10 * k)
        raw = data[file_offset:file_offset + file_sizes[k]]
        segments.append((memory_offset, lz4_block(raw, size) if flags & (1 << k) else raw))
    image = bytearray(max(offset + len(b) for offset, b in segments))
    for offset, b in segments:
        image[offset:offset + len(b)] = b
    return bytes(image), len(segments[0][1])


def relative_relocations(image):
    """{slot: addend} of the R_AARCH64_RELATIVE entries, read through MOD0 and the dynamic section"""
    mod0 = struct.unpack_from('<I', image, 4)[0]
    if image[mod0:mod0 + 4] != b'MOD0':
        raise SystemExit('the sdk module has no MOD0 header')
    at = mod0 + struct.unpack_from('<i', image, mod0 + 4)[0]
    tags = {}
    while True:
        tag, value = struct.unpack_from('<qQ', image, at)
        at += 16
        if tag == 0:
            break
        tags.setdefault(tag, value)
    relocations = {}
    for i in range(tags[DT_RELASZ] // 24):
        slot, info, addend = struct.unpack_from('<QQq', image, tags[DT_RELA] + 24 * i)
        if info & 0xffffffff == R_AARCH64_RELATIVE:
            relocations[slot] = addend
    return relocations


def name_blob(image):
    """The sorted names of the NVN library (without the nvn prefix): the blob of NUL separated names around an ANCHOR
    that holds at least MIN_NAMES names in order. Other strings with the same text are skipped."""
    at = image.find(ANCHOR)
    while at >= 0:
        start = at
        while start > 0 and (chr(image[start - 1]).isalnum() or image[start - 1] == 0):
            start -= 1
        end = at + 1
        while end < len(image) and (chr(image[end]).isalnum() or image[end] == 0):
            end += 1
        names = [n.decode() for n in image[start:end].split(b'\0') if n]
        first = names[0]
        names[0] = first[len(first) - len(first.lstrip('0123456789')):]  # a digit before the blob is other data
        if len(names) >= MIN_NAMES and names == sorted(names):
            return names
        at = image.find(ANCHOR, at + 1)
    raise SystemExit('the sdk module has no sorted NVN name table')


def function_table(image, text_size, relocations, names):
    """The function offsets in the order of names: the run of relocated pointers whose debug label setters are all ret"""
    labels = [i for i, n in enumerate(names) if n.endswith('SetDebugLabel')]
    found = []
    for slot in sorted(relocations):
        if slot - 8 in relocations:
            continue  # not the start of a run
        targets = [relocations.get(slot + 8 * i) for i in range(len(names))]
        if any(t is None or not 0 <= t < text_size or t % 4 for t in targets):
            continue
        if labels and all(struct.unpack_from('<I', image, targets[i])[0] == RET for i in labels):
            found.append(targets)
    if len(found) != 1:
        raise SystemExit('expected one NVN function table in the sdk module, found %d' % len(found))
    return found[0]


def from_sdk(path, base):
    """{name: address} from the sdk module"""
    image, text_size = nso_image(path)
    names = name_blob(image)
    table = function_table(image, text_size, relative_relocations(image), names)
    return {'nvn' + n: base + f for n, f in zip(names, table)}


# --- the other inputs ---

def from_log(path):
    """({name: address}, {name: calls} or None) from a log with nvn_trace=1"""
    resolved, used = {}, {}
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = re.search(r'\bnvn: (nvn\w+) -> ([0-9a-fA-F]+)\s*$', line)
            if m:
                resolved.setdefault(m.group(1), int(m.group(2), 16))
            m = re.search(r'\bnvn-used: (nvn\w+) (\d+)', line)
            if m:
                used[m.group(1)] = int(m.group(2))
    return resolved, used or None


def calls_doc(path):
    """{name: calls} from the list in docs/nvn-calls.md"""
    calls = {}
    with open(path, encoding='utf-8') as f:
        for line in f:
            m = re.match(r'^(nvn\w+) (\d+)\s*$', line)
            if m:
                calls[m.group(1)] = int(m.group(2))
    return calls


def handlers(path):
    """{name: handler function} of the table native_procs"""
    with open(path, encoding='utf-8') as f:
        text = f.read()
    table = re.search(r'native_procs\[\] = \{(.*?)\n\};', text, re.S)
    if not table:
        raise SystemExit('nvn_native.c has no table native_procs')
    return dict(re.findall(r'\{"(nvn\w+)", (\w+)\}', table.group(1)))


def existing_rows(path):
    """{name: {column: text}} of the coverage file, or {} when it does not exist"""
    rows = {}
    if not os.path.isfile(path):
        return rows
    with open(path, encoding='utf-8') as f:
        for line in f:
            cells = [c.strip() for c in line.strip().strip('|').split('|')]
            if len(cells) == len(COLUMNS) and cells[0].startswith('`nvn'):
                rows[cells[0].strip('`')] = dict(zip(COLUMNS, cells))
    return rows


# --- the groups of docs/nvn-design.md (2-6). A function that no run called is G10. ---

G6 = {'Initialize', 'Finalize', 'AddCommandMemory', 'AddControlMemory', 'SetMemoryCallback', 'SetMemoryCallbackData',
      'BeginRecording', 'EndRecording', 'CallCommands', 'SetTexturePool', 'SetSamplerPool', 'SetRenderTargets',
      'SetViewports', 'SetScissors', 'ClearColor', 'Barrier'}
G7 = {'nvnWindowAcquireTexture', 'nvnQueueSubmitCommands', 'nvnQueueFenceSync', 'nvnQueuePresentTexture'}
G9_COMMANDS = ('ClearTexture', 'ClearDepthStencil', 'CopyTextureToTexture', 'DispatchCompute', 'TransformFeedback',
               'ReportCounter', 'ResetCounter', 'Image')
STATES = ('BlendState', 'ColorState', 'PolygonState', 'DepthStencilState', 'MultisampleState', 'ChannelMaskState',
          'VertexAttribState', 'VertexStreamState')
G3_TEXTURE = {'Initialize', 'Finalize', 'GetWidth', 'GetHeight', 'GetDepth', 'GetTarget', 'GetStride'}


def group(name, calls):
    if not calls:
        return 'G10'
    base = name[3:]
    if name in G7:
        return 'G7'
    if base.startswith('CommandBuffer'):
        rest = base[len('CommandBuffer'):]
        if rest in G6:
            return 'G6'
        if any(k in rest for k in G9_COMMANDS):
            return 'G9'
        return 'G8'
    if base in ('TexturePoolInitialize', 'TexturePoolGetSize', 'SamplerPoolInitialize', 'SamplerPoolGetSize'):
        return 'G5'
    if base.startswith(('TexturePool', 'TextureView', 'Sampler')) or 'Image' in base or 'TextureHandle' in base:
        return 'G9'
    if base.startswith(('TextureWriteTexels', 'TextureFlushTexels')):
        return 'G9'
    if base.startswith('TextureBuilder') or (base.startswith('Texture') and base[len('Texture'):] in G3_TEXTURE):
        return 'G3'
    if base.startswith('MemoryPool'):
        return 'G2'
    if base.startswith(('DeviceBuilder', 'Device', 'QueueBuilder', 'Queue')):
        return 'G1'
    if base.startswith(('WindowBuilder', 'Window', 'Sync')):
        return 'G4'
    if base.startswith(('Buffer', 'Program')) or base.startswith(STATES):
        return 'G8'
    return 'G9'


def handler_state(name, table):
    """none: no host code names it; stub-log: the missing-function handler answers; implemented: a host handler"""
    return 'implemented' if name in table else 'stub-log'


# --- the file ---

HEADER = """<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# NVN coverage of the host layer

Made by `tools/nvn_coverage.py` (step 4-u1 of `docs/nvn-design.md`, section "Unobserved functions"). Do not edit the
columns function to handler by hand: run the tool again. The columns checks, seen and notes are written by hand and the
tool keeps them.

- function, address: the {count} functions that the game resolves, from the `nvn:` lines of a log with `nvn_trace=1` or
  from the name table of the NVN library in the sdk module. The address is the guest address in the update program of
  update 1.1.1.
- group: G1 to G9 as in `docs/nvn-design.md`, section 2-6. G10 is a function that no run called.
- calls: the calls of {calls_source}, through the dispatch table and with the library (`nvn_native=0`).
- handler: `implemented` when `native_procs` of `nvn_native.c` has a host handler, `stub-log` when the missing-function
  handler answers (the first call is logged and 0 is returned).
- checks: the checks that passed, with their scope (`unit`, `selftest R/M/S/Y` with the build, `exec`). seen: a game run
  with `nvn_native=1` that called it and was read. A function with checks and without seen is not verified in the game.

## Summary

{summary}

## Functions

"""


def summary(rows):
    groups = sorted({r['group'] for r in rows.values()}, key=lambda g: int(g[1:]))
    lines = ['| group | functions | implemented | stub-log | called | called and stub-log |',
             '| --- | --- | --- | --- | --- | --- |']
    total = [0, 0, 0, 0, 0]
    for g in groups:
        members = [r for r in rows.values() if r['group'] == g]
        implemented = sum(r['handler'] == 'implemented' for r in members)
        stub = sum(r['handler'] == 'stub-log' for r in members)
        called = sum(r['calls'] not in ('0', '') for r in members)
        gap = sum(r['handler'] == 'stub-log' and r['calls'] not in ('0', '') for r in members)
        for i, v in enumerate((len(members), implemented, stub, called, gap)):
            total[i] += v
        lines.append('| %s | %d | %d | %d | %d | %d |' % (g, len(members), implemented, stub, called, gap))
    lines.append('| all | %d | %d | %d | %d | %d |' % tuple(total))
    return '\n'.join(lines)


def render(rows, calls_source):
    order = sorted(rows.values(), key=lambda r: (int(r['group'][1:]), r['function']))
    table = ['| ' + ' | '.join(COLUMNS) + ' |', '| ' + ' | '.join('---' for _ in COLUMNS) + ' |']
    for r in order:
        cells = ['`%s`' % r['function']] + [r[c] for c in COLUMNS[1:]]
        table.append('| ' + ' | '.join(cells) + ' |')
    return HEADER.format(count=len(rows), calls_source=calls_source,
                         summary=summary(rows)) + '\n'.join(table) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--log', help='a log of a run with nvn_trace=1')
    ap.add_argument('--sdk', help='the sdk file of the ExeFS (an NSO)')
    ap.add_argument('--sdk-base', type=lambda s: int(s, 0), default=SDK_BASE, help='the guest address of sdk')
    ap.add_argument('--check', action='store_true', help='write nothing, exit 1 when the file would change')
    args = ap.parse_args()

    old = existing_rows(COVERAGE)
    resolved, used = from_log(args.log) if args.log else ({}, None)
    if not resolved and args.sdk:
        resolved = from_sdk(args.sdk, args.sdk_base)
    elif not resolved and old:
        resolved = {n: int(r['address'], 16) for n, r in old.items()}
    if not resolved:
        raise SystemExit('no names: give --log or --sdk (the coverage file does not exist yet)')
    if used is not None:
        calls, calls_source = used, 'the `nvn-used:` lines of the log'
    else:
        calls, calls_source = calls_doc(CALLS), 'the run in `docs/nvn-calls.md`'
    unknown = sorted(set(calls) - set(resolved))
    if unknown:
        raise SystemExit('called but not resolved: %s' % ', '.join(unknown))

    table = handlers(NATIVE)
    stray = sorted(set(table) - set(resolved))
    if stray:
        print('nvn_coverage: handlers for names the game does not resolve: %s' % ', '.join(stray), file=sys.stderr)
    rows = {}
    for name, address in resolved.items():
        n = calls.get(name, 0)
        row = {'function': name, 'group': group(name, n), 'address': '%x' % address, 'calls': str(n),
               'handler': handler_state(name, table)}
        for column in MANUAL:
            row[column] = old.get(name, {}).get(column, '')
        rows[name] = row
    text = render(rows, calls_source)

    current = open(COVERAGE, encoding='utf-8').read() if os.path.isfile(COVERAGE) else ''
    if args.check:
        sys.exit(0 if current == text else 1)
    if current != text:
        with open(COVERAGE, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    gap = [r['function'] for r in rows.values() if r['handler'] == 'stub-log' and r['calls'] != '0']
    print('nvn_coverage: %d functions, %d implemented, %d called without a handler%s' %
          (len(rows), sum(r['handler'] == 'implemented' for r in rows.values()), len(gap),
           (': ' + ', '.join(sorted(gap))) if gap else ''))


if __name__ == '__main__':
    main()
