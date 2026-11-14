#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Compares the draw record files of a run with nvn_native=0 and a run with nvn_native=1.

    tools/drawrec_compare.py PREFIX_A PREFIX_B [--skip N] [--top N] [--ratio R]

PREFIX is a path without the .frames.txt and .sigs.txt suffix, for example
game/Arceus/Arceus.drawrec0. The script adds the two file names itself. A is the reference run
(normally drawrec0), B the other. The frames of the two runs do not align, so the script compares
counts per present. --skip leaves out the first frames of the count statistics (the load screens),
--top sets the rows per list, and --ratio R marks a shared signature as off when its rate ratio is
above R or below 1/R. The script prints to stdout only and writes no file.
"""

import argparse
import math
import re
import statistics
from collections import namedtuple

# the frame line: the frame number, 7 call counts in KINDS order, 3 hash words
FRAME_FIELDS = 11
KINDS = ('draw', 'clear', 'dispatch', 'blit', 'dma_copy', 'blit_mem', 'draw_texture')
PRESENT_KIND = 'present'
SIG_KINDS = KINDS + (PRESENT_KIND,)
DEFAULT_SKIP = 0
DEFAULT_TOP = 30
DEFAULT_RATIO = 2.0
HEX16 = re.compile(r'[0-9a-fA-F]{16}')

Frame = namedtuple('Frame', 'number counts ordered summed present')
SigsHeader = namedtuple('SigsHeader', 'frames dropped shader_uncached')
Sig = namedtuple('Sig', 'sig count text kind')
Shared = namedtuple('Shared', 'ratio rate_a rate_b entry')

FRAMES_SUFFIX = '.frames.txt'
SIGS_SUFFIX = '.sigs.txt'


def parse_count(text, what):
    """One decimal count of a record line."""
    if not (text.isascii() and text.isdigit()):
        raise ValueError('%s is not a count: %r' % (what, text))
    return int(text)


def parse_hex16(text, what):
    """One 64-bit word written as 16 hex digits."""
    if not HEX16.fullmatch(text):
        raise ValueError('%s is not a 16-digit hex value: %r' % (what, text))
    return int(text, 16)


def parse_frame_line(line):
    """A line of the frame file: <frame> <7 counts> <ordered> <sum> <present signature>."""
    fields = line.split()
    if len(fields) != FRAME_FIELDS:
        raise ValueError('frame line has %d fields, expect %d' % (len(fields), FRAME_FIELDS))
    number = parse_count(fields[0], 'frame number')
    counts = tuple(parse_count(fields[i + 1], '%s count' % kind) for i, kind in enumerate(KINDS))
    ordered = parse_hex16(fields[8], 'ordered hash')
    summed = parse_hex16(fields[9], 'sum hash')
    present = parse_hex16(fields[10], 'present signature')
    return Frame(number, counts, ordered, summed, present)


def parse_sigs_header(line):
    """The first line of the signature file: frames <F> dropped <D> shader_uncached <U>."""
    fields = line.split()
    if len(fields) != 6 or (fields[0], fields[2], fields[4]) != ('frames', 'dropped', 'shader_uncached'):
        raise ValueError('sigs header is not "frames <F> dropped <D> shader_uncached <U>"')
    return SigsHeader(parse_count(fields[1], 'F'), parse_count(fields[3], 'dropped'),
                      parse_count(fields[5], 'shader_uncached'))


def parse_sig_line(line):
    """A signature line: <sig> <count> <text>. The text runs to the end of the line and can have spaces."""
    fields = line.rstrip('\r\n').split(None, 2)
    if len(fields) != 3:
        raise ValueError('signature line has %d fields, expect 3' % len(fields))
    sig = parse_hex16(fields[0], 'signature')
    count = parse_count(fields[1], 'count')
    # the runtime adds an entry at its first call, so a rate (and a ratio of rates) is never 0
    if count == 0:
        raise ValueError('signature count is 0')
    text = fields[2].rstrip()
    kind = text.split(None, 1)[0]
    if kind not in SIG_KINDS:
        raise ValueError('unknown kind %r in the signature text' % kind)
    return Sig(sig, count, text, kind)


def read_lines(path):
    try:
        with open(path, encoding='utf-8', errors='replace') as record:
            return record.readlines()
    except OSError as err:
        raise SystemExit('%s: cannot read: %s' % (path, err.strerror or err)) from None


def load_frames(path):
    """All frames of a frame file, in file order."""
    frames = []
    for lineno, line in enumerate(read_lines(path), 1):
        try:
            frames.append(parse_frame_line(line))
        except ValueError as err:
            raise SystemExit('%s:%d: %s' % (path, lineno, err)) from None
    return frames


def load_sigs(path):
    """The header and the signatures of a signature file. A repeated signature is an error."""
    lines = read_lines(path)
    if not lines:
        raise SystemExit('%s:1: empty file, expect the header line' % path)
    try:
        header = parse_sigs_header(lines[0])
    except ValueError as err:
        raise SystemExit('%s:1: %s' % (path, err)) from None
    entries = {}
    for lineno, line in enumerate(lines[1:], 2):
        try:
            entry = parse_sig_line(line)
        except ValueError as err:
            raise SystemExit('%s:%d: %s' % (path, lineno, err)) from None
        if entry.sig in entries:
            raise SystemExit('%s:%d: signature %016x appears again' % (path, lineno, entry.sig))
        entries[entry.sig] = entry
    return header, list(entries.values())


def count_statistics(frames, skip):
    """Median, minimum and maximum of each call count per frame, after the first skip frames."""
    if skip < 0:
        raise ValueError('--skip must not be negative')
    kept = frames[skip:]
    if not kept:
        raise ValueError('no frame left after skipping %d' % skip)
    rows = []
    for i, kind in enumerate(KINDS):
        values = [frame.counts[i] for frame in kept]
        rows.append((kind, statistics.median(values), min(values), max(values)))
    return rows


def kind_counts(entries):
    """The number of signatures of each kind."""
    counts = dict.fromkeys(SIG_KINDS, 0)
    for entry in entries:
        counts[entry.kind] += 1
    return counts


def one_run_only(entries, other_sigs):
    """The signatures of one run that the other run does not have."""
    return [entry for entry in entries if entry.sig not in other_sigs]


def rank_by_rate(entries, frames, top):
    """The signatures by rate, highest first, at most top rows."""
    if frames == 0:
        raise ValueError('F is 0, so a rate is undefined')
    rows = sorted(entries, key=lambda entry: (-entry.count / frames, entry.sig))
    return rows[:top]


def shared_rows(entries_a, entries_b, frames_a, frames_b, ratio_limit):
    """The shared signatures and the subset that is off. A ratio above R or below 1/R is off."""
    if frames_a == 0 or frames_b == 0:
        raise ValueError('F is 0, so a rate is undefined')
    if not math.isfinite(ratio_limit) or ratio_limit <= 0:
        raise ValueError('--ratio must be a finite number above 0')
    by_sig_b = {entry.sig: entry for entry in entries_b}
    rows = []
    for entry_a in entries_a:
        entry_b = by_sig_b.get(entry_a.sig)
        if entry_b is None:
            continue
        rate_a = entry_a.count / frames_a
        rate_b = entry_b.count / frames_b
        rows.append(Shared(rate_b / rate_a, rate_a, rate_b, entry_a))
    off = [row for row in rows if row.ratio > ratio_limit or row.ratio < 1.0 / ratio_limit]
    off.sort(key=lambda row: (-abs(math.log(row.ratio)), row.entry.sig))
    return len(rows), off


def coverage(entries, shared_sigs):
    """Percent of a run's recorded calls that are in signatures shared with the other run, present left out."""
    total = 0
    shared = 0
    for entry in entries:
        if entry.kind == PRESENT_KIND:
            continue
        total += entry.count
        if entry.sig in shared_sigs:
            shared += entry.count
    if total == 0:
        raise ValueError('no recorded call to cover')
    return 100.0 * shared / total


def format_number(value):
    """A count or a median without trailing zeros."""
    if isinstance(value, int):
        return str(value)
    return '%g' % value


def format_kind_counts(counts):
    return ', '.join('%s %d' % (kind, counts[kind]) for kind in SIG_KINDS)


def print_run(letter, prefix, frames, header, entries):
    print('%s %s' % (letter, prefix))
    print('  frame lines %d' % len(frames))
    print('  sigs frames %d dropped %d shader_uncached %d' % (header.frames, header.dropped,
                                                              header.shader_uncached))
    print('  signatures %d' % len(entries))


def print_statistics(stats_a, stats_b, skip):
    print('[2] calls per frame (skip %d)' % skip)
    print('%-12s %10s %8s %8s %10s %8s %8s' % ('kind', 'A median', 'A min', 'A max', 'B median', 'B min',
                                               'B max'))
    for (kind, median_a, min_a, max_a), (_, median_b, min_b, max_b) in zip(stats_a, stats_b):
        print('%-12s %10s %8s %8s %10s %8s %8s' % (
            kind, format_number(median_a), min_a, max_a, format_number(median_b), min_b, max_b))


def print_one_run_only(letter, only, frames, top):
    print('only in %s: %d (%s)' % (letter, len(only), format_kind_counts(kind_counts(only))))
    print('%-9s %8s  %-16s  %s' % ('rate', 'count', 'signature', 'text'))
    for entry in rank_by_rate(only, frames, top):
        print('%-9.3f %8d  %016x  %s' % (entry.count / frames, entry.count, entry.sig, entry.text))


def print_shared(shared_count, off, top):
    print('[4] shared signatures')
    print('shared %d, off %d' % (shared_count, len(off)))
    print('%-9s %9s %9s  %-16s  %s' % ('ratio', 'rate A', 'rate B', 'signature', 'text'))
    for row in off[:top]:
        print('%-9.3f %9.3f %9.3f  %016x  %s' % (row.ratio, row.rate_a, row.rate_b, row.entry.sig,
                                                 row.entry.text))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('prefix_a', help='the reference run, normally drawrec0')
    parser.add_argument('prefix_b', help='the other run, normally drawrec1')
    parser.add_argument('--skip', type=int, default=DEFAULT_SKIP,
                        help='frames at the start of the frame file to leave out (default %d)' % DEFAULT_SKIP)
    parser.add_argument('--top', type=int, default=DEFAULT_TOP,
                        help='rows per list (default %d)' % DEFAULT_TOP)
    parser.add_argument('--ratio', type=float, default=DEFAULT_RATIO,
                        help='rate ratio that marks a shared signature as off (default %g)' % DEFAULT_RATIO)
    args = parser.parse_args()
    if args.top < 0:
        raise SystemExit('--top must not be negative')

    prefix_a = args.prefix_a
    prefix_b = args.prefix_b
    frames_a = load_frames(prefix_a + FRAMES_SUFFIX)
    frames_b = load_frames(prefix_b + FRAMES_SUFFIX)
    header_a, sigs_a = load_sigs(prefix_a + SIGS_SUFFIX)
    header_b, sigs_b = load_sigs(prefix_b + SIGS_SUFFIX)

    print('draw record compare')
    print('[1] runs')
    print_run('A', prefix_a, frames_a, header_a, sigs_a)
    print_run('B', prefix_b, frames_b, header_b, sigs_b)

    if header_a.dropped:
        print('warning: A: dropped %d signatures, the signature table was full, the compare is not complete'
              % header_a.dropped)
    if header_b.dropped:
        print('warning: B: dropped %d signatures, the signature table was full, the compare is not complete'
              % header_b.dropped)
    if header_a.frames < len(frames_a):
        print('warning: A: the sigs file has %d frames, the frames file has %d lines, the sigs file is older'
              % (header_a.frames, len(frames_a)))
    if header_b.frames < len(frames_b):
        print('warning: B: the sigs file has %d frames, the frames file has %d lines, the sigs file is older'
              % (header_b.frames, len(frames_b)))

    print()
    try:
        stats_a = count_statistics(frames_a, args.skip)
        stats_b = count_statistics(frames_b, args.skip)
    except ValueError as err:
        raise SystemExit(str(err)) from None
    print_statistics(stats_a, stats_b, args.skip)

    print()
    print('[3] signatures in one run only')
    sigs_of_b = {entry.sig for entry in sigs_b}
    sigs_of_a = {entry.sig for entry in sigs_a}
    only_a = one_run_only(sigs_a, sigs_of_b)
    only_b = one_run_only(sigs_b, sigs_of_a)
    try:
        print_one_run_only('A', only_a, header_a.frames, args.top)
        print()
        print_one_run_only('B', only_b, header_b.frames, args.top)
    except ValueError as err:
        raise SystemExit(str(err)) from None

    print()
    try:
        shared_count, off = shared_rows(sigs_a, sigs_b, header_a.frames, header_b.frames, args.ratio)
    except ValueError as err:
        raise SystemExit(str(err)) from None
    print_shared(shared_count, off, args.top)

    print()
    print('[5] coverage (calls in shared signatures, present left out)')
    shared_sigs = sigs_of_a & sigs_of_b
    try:
        cover_a = coverage(sigs_a, shared_sigs)
        cover_b = coverage(sigs_b, shared_sigs)
    except ValueError as err:
        raise SystemExit(str(err)) from None
    print('A %.1f%%' % cover_a)
    print('B %.1f%%' % cover_b)


if __name__ == '__main__':
    main()
