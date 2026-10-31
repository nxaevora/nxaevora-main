#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Finds the direct calls (bl) and jumps (b) to given guest addresses in the code of the extracted game.

    SNX_SCARLETNX=<abs path of scarletnx.exe> python tools/find_callers.py ADDRESS... [--game DIR]

Run it from the repository root in the MSYS2 CLANG64 shell, as tools/jump_table_scan.py is run: it uses ScarletNX's
tools/moduledump.py. ADDRESS is a guest address in hex. The default game folder is game/work/Arceus.
Only the text segment of each module is scanned (its range comes from the NSO header in exefs/).
A call through a function pointer, a vtable or a PLT stub is not found: name the stub and search for the stub.
"""

import argparse
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path.insert(0, os.path.join(ROOT, 'third_party', 'ScarletNX', 'tools'))
import moduledump  # noqa: E402  (ScarletNX's tool, found by the path above)

DEFAULT_GAME = os.path.join(ROOT, 'game', 'work', 'Arceus')
INSN_BYTES = 4
OPCODE_SHIFT = 26  # the top six bits of an A64 branch word hold its opcode
IMM26_MASK = (1 << OPCODE_SHIFT) - 1
IMM26_SIGN = 1 << (OPCODE_SHIFT - 1)
OPCODE_B, OPCODE_BL = 0x05, 0x25
NSO_MAGIC = b'NSO0'
NSO_TEXT_HEADER = 0x10  # the text segment header: file offset, memory offset, size (3 words)


def text_range(game, module):
    """(start, end) of the text segment as offsets in the module image, from the NSO header of its ExeFS file"""
    path = os.path.join(game, 'exefs', module.name)
    with open(path, 'rb') as f:
        header = f.read(NSO_TEXT_HEADER + 12)
    if header[:4] != NSO_MAGIC:
        raise SystemExit('%s is not an NSO: cannot find the text segment of %s' % (path, module.name))
    _, mem_offset, size = struct.unpack_from('<3I', header, NSO_TEXT_HEADER)
    return mem_offset, min(mem_offset + size, len(module.image))


def branch_target(word, pc):
    """(name, target) when the word is a bl or b, else None"""
    opcode = word >> OPCODE_SHIFT
    if opcode not in (OPCODE_B, OPCODE_BL):
        return None
    offset = word & IMM26_MASK
    if offset & IMM26_SIGN:
        offset -= 1 << OPCODE_SHIFT
    return ('bl' if opcode == OPCODE_BL else 'b'), pc + offset * INSN_BYTES


def find_callers(module, start, end, targets):
    """The (name, pc, target) of each bl and b in the text range of the module that reaches a target"""
    count = (end - start) // INSN_BYTES
    words = struct.unpack_from('<%dI' % count, module.image, start)
    for index, word in enumerate(words):
        pc = module.base + start + index * INSN_BYTES
        branch = branch_target(word, pc)
        if branch and branch[1] in targets:
            yield branch[0], pc, branch[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('addresses', nargs='+', help='guest addresses in hex')
    parser.add_argument('--game', default=DEFAULT_GAME)
    args = parser.parse_args()
    targets = {int(a, 16) for a in args.addresses}
    modules, _ = moduledump.load_program(args.game)
    for module in modules:
        start, end = text_range(args.game, module)
        for name, pc, target in find_callers(module, start, end, targets):
            print('%s %s at %x -> %x' % (module.name, name, pc, target))


if __name__ == '__main__':
    main()
