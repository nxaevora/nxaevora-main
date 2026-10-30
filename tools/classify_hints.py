#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Sorts the pcs of a game's .hints file by module.

    tools/classify_hints.py game/recompiled/<id> [hints-name]

The module bases come from the "Loaded <module> at 0x<base>" lines of the game log in the same folder.
The default hints file is <id>.hints. Pass another name to read the hints file of an update program.
"""

import bisect
import collections
import os
import re
import sys

LOADED = re.compile(r'Loaded (\S+) at 0x([0-9a-fA-F]+)')


def read_bases(log_path):
    bases = []
    with open(log_path, encoding='utf-8', errors='replace') as log:
        for line in log:
            match = LOADED.search(line)
            if match:
                bases.append((int(match.group(2), 16), match.group(1)))
    return sorted(set(bases))


def read_hints(path):
    with open(path, encoding='ascii') as hints_file:
        return sorted({int(line, 16) for line in hints_file.read().split()})


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    folder = sys.argv[1]
    game = os.path.basename(os.path.normpath(folder))
    hints_name = sys.argv[2] if len(sys.argv) == 3 else game
    log_path = os.path.join(folder, game + '.log')
    hints_path = os.path.join(folder, hints_name + '.hints')
    for path in (log_path, hints_path):
        if not os.path.isfile(path):
            raise SystemExit('missing file: ' + path)
    bases = read_bases(log_path)
    if not bases:
        raise SystemExit('no "Loaded <module> at" lines in ' + log_path)
    starts = [base for base, _ in bases]
    counts = collections.Counter()
    hints = read_hints(hints_path)
    for pc in hints:
        index = bisect.bisect_right(starts, pc) - 1
        if index < 0:
            print('%#011x  outside the modules' % pc)
            continue
        base, name = bases[index]
        counts[name] += 1
        print('%#011x  %-8s +%#x' % (pc, name, pc - base))
    print('%d pcs: %s' % (len(hints), dict(counts)))


if __name__ == '__main__':
    main()
