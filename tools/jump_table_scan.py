#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Finds the jump tables of a game's AArch64 modules and lists the targets of their BR instructions.

    tools/jump_table_scan.py <extracted game folder> [--module NAME ...] [--hints FILE [--merge]] [--check HEXPC ...]

The folder holds exefs/ (the layout of the program that you analyze). SNX_SCARLETNX must name scarletnx and the
MSYS2 tools must be on PATH, as for ScarletNX's tools/symbols.py. --hints writes the targets, one hex pc on each
line, in the format of a .hints file. --merge keeps the pcs that the file holds already (the misses that the runtime
recorded). --check says which table holds each pc.

Two compiler patterns are found: a table of bytes, halves or words that is added to a base address (shifted by the
entry size), and a signed table that is relative to its own address.
"""

import argparse
import array
import os
import shutil
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'third_party', 'ScarletNX', 'tools'))
import moduledump  # noqa: E402

WINDOW = 24  # instructions searched before a BR for the table address, the load and the add
BOUND_WINDOW = 40  # instructions searched before a BR for the range check (cmp and b.hi or b.hs)
REGISTER_SEARCH = 4000  # instructions searched back for the address that a table register holds
MAX_ENTRIES = 256  # entries read from a table without a range check
MAX_REACH = 0x100000  # a case block lies within this distance of its BR

# register-offset loads: code (w & 0xffe00c00) -> (entry bytes, signed)
LOADS = {
    0x38600800: (1, False), 0x38e00800: (1, True), 0x38a00800: (1, True),
    0x78600800: (2, False), 0x78e00800: (2, True), 0x78a00800: (2, True),
    0xb8600800: (4, False), 0xb8a00800: (4, True),
}
COND_HI, COND_HS = 8, 2


def sign_extend(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def pc_relative(word, pc):
    """The address that an ADR or ADRP puts in a register, or None for another word"""
    kind = word & 0x9f000000
    if kind not in (0x10000000, 0x90000000):
        return None
    imm = sign_extend(((word >> 5) & 0x7ffff) << 2 | ((word >> 29) & 3), 21)
    return pc + imm if kind == 0x10000000 else (pc & ~0xfff) + (imm << 12)


def range_check(words, index):
    """The entry count that a CMP and a B.HI or B.HS before the BR allow, or None"""
    for j in range(index - 1, max(index - BOUND_WINDOW, 0), -1):
        word = words[j]
        if (word & 0x7f80001f) != 0x7100001f or (word >> 22) & 1:
            continue
        limit = (word >> 10) & 0xfff
        for k in range(j + 1, min(j + 4, index)):
            if (words[k] & 0xff000010) == 0x54000000:
                cond = words[k] & 15
                if cond == COND_HI:
                    return limit + 1
                if cond == COND_HS:
                    return limit
    return None


def find_spec(words, base, index):
    """(table, entry bytes, signed, base address, shift) for the BR at words[index], or None"""
    regs = {}
    br_reg = (words[index] >> 5) & 31
    for j in range(max(index - WINDOW, 0), index):
        word, pc = words[j], base + 4 * j
        rd, rn, rm = word & 31, (word >> 5) & 31, (word >> 16) & 31
        target = pc_relative(word, pc)
        if target is not None:
            regs[rd] = ('addr', target)
        elif (word & 0xff800000) == 0x91000000 and regs.get(rn, ('',))[0] == 'addr':
            regs[rd] = ('addr', regs[rn][1] + (((word >> 10) & 0xfff) << (12 if (word >> 22) & 1 else 0)))
        elif (word & 0xffe0ffe0) == 0xaa0003e0 and rm in regs:
            regs[rd] = regs[rm]
        elif (word & 0xffe00c00) in LOADS and rn != 31 and regs.get(rn, ('',))[0] != 'entry':
            size, signed = LOADS[word & 0xffe00c00]
            known = regs.get(rn)
            table = known[1] if known and known[0] == 'addr' else ('register', rn, j)
            regs[rd] = ('entry', table, size, signed)
        elif (word & 0xff200000) == 0x8b000000 and not (word >> 22) & 3:
            set_register(regs, rd, combine(regs.get(rn), regs.get(rm), (word >> 10) & 0x3f))
        elif (word & 0xffe00000) == 0x8b200000 and ((word >> 13) & 7) in (2, 3, 6, 7) and (word >> 10) & 7 <= 4:
            set_register(regs, rd, combine(regs.get(rn), regs.get(rm), (word >> 10) & 7))
    value = regs.get(br_reg)
    return value[1:] if value and value[0] == 'table' else None


def resolve_register(words, base, index, register):
    """The address that ADRP+ADD or ADR put in the register before words[index], or None"""
    for j in range(index - 1, max(index - REGISTER_SEARCH, 0), -1):
        word = words[j]
        if (word & 31) != register:
            continue
        target = pc_relative(word, base + 4 * j)
        if target is None:
            continue
        if (word & 0x9f000000) == 0x10000000:
            return target
        for k in range(j + 1, min(j + 4, index)):
            follow = words[k]
            if (follow & 0xff80001f) == (0x91000000 | register) and ((follow >> 5) & 31) == register:
                return target + (((follow >> 10) & 0xfff) << (12 if (follow >> 22) & 1 else 0))
        return None
    return None


def set_register(regs, number, value):
    if value:
        regs[number] = value
    else:
        regs.pop(number, None)


def combine(first, second, shift):
    """first + (second << shift): a base address plus a loaded entry makes a jump-table value"""
    if first and second and first[0] == 'addr' and second[0] == 'entry':
        return ('table', second[1], second[2], second[3], first[1], shift)
    return None


def read_entries(module, spec, br_pc, count):
    """The targets of a table, or None when an entry is not a plausible target"""
    table, size, signed, base_address, shift = spec
    offset = table - module.base
    if offset < 0 or offset + size > len(module.image):
        return None
    limit = count if count else MAX_ENTRIES
    targets = []
    for i in range(limit):
        start = offset + i * size
        if start + size > len(module.image):
            break
        entry = int.from_bytes(module.image[start:start + size], 'little', signed=signed)
        target = (base_address + (entry << shift)) & 0xffffffffffffffff
        valid = target % 4 == 0 and module.base <= target < module.base + len(module.image) \
            and abs(target - br_pc) <= MAX_REACH
        if not valid:
            if count:
                return None
            break
        targets.append(target)
    return targets or None


def scan(module):
    words = array.array('I')
    words.frombytes(module.image[:len(module.image) & ~3])
    tables = []
    for index, word in enumerate(words):
        if (word & 0xfffffc1f) != 0xd61f0000:
            continue
        spec = find_spec(words, module.base, index)
        if spec is None:
            continue
        if isinstance(spec[0], tuple):
            table = resolve_register(words, module.base, spec[0][2], spec[0][1])
            if table is None:
                continue
            spec = (table,) + spec[1:]
        pc = module.base + 4 * index
        count = range_check(words, index)
        targets = read_entries(module, spec, pc, count)
        if targets:
            tables.append({'br': pc, 'table': spec[0], 'bounded': count is not None, 'targets': targets})
    return tables


def write_hints(path, targets, merge):
    """Writes the pcs as a hints file. With merge, the pcs of the existing file stay, and it is copied to path.bak."""
    pcs = set(targets)
    if merge and os.path.isfile(path):
        with open(path, encoding='ascii') as old:
            pcs.update(int(line, 16) for line in old.read().split())
        shutil.copyfile(path, path + '.bak')
    with open(path, 'w', encoding='ascii') as out:
        out.write(''.join('%x\n' % pc for pc in sorted(pcs)))
    return len(pcs)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('program')
    parser.add_argument('--module', action='append', help='a module to scan (default: all but rtld)')
    parser.add_argument('--hints', help='write the targets to this file')
    parser.add_argument('--merge', action='store_true',
                        help='keep the pcs that --hints already holds (a copy goes to FILE.bak)')
    parser.add_argument('--check', nargs='+', help='hex pcs: tell which table holds each')
    args = parser.parse_args()
    if not os.path.isdir(args.program):
        raise SystemExit('not a folder: ' + args.program)
    modules, is64 = moduledump.load_program(args.program)
    if not is64:
        raise SystemExit('only AArch64 programs are supported')
    wanted = args.module or [m.name for m in modules if m.name != 'rtld']
    found = {}
    for module in modules:
        if module.name in wanted:
            found[module.name] = scan(module)
            count = sum(len(t['targets']) for t in found[module.name])
            print('%-8s %6d tables, %7d targets' % (module.name, len(found[module.name]), count))
    all_targets = sorted({x for tables in found.values() for t in tables for x in t['targets']})
    print('%d different targets' % len(all_targets))
    if args.hints:
        written = write_hints(args.hints, all_targets, args.merge)
        print('%d pcs written to %s' % (written, args.hints))
    for text in args.check or []:
        pc = int(text, 16)
        hits = [(name, t) for name, tables in found.items() for t in tables if pc in t['targets']]
        for name, t in hits:
            print('%#x: %s, BR at %#x, table at %#x, entry %d of %d%s' % (
                pc, name, t['br'], t['table'], t['targets'].index(pc), len(t['targets']),
                '' if t['bounded'] else ' (no range check)'))
        if not hits:
            print('%#x: not a target of a table that was found' % pc)


if __name__ == '__main__':
    main()
