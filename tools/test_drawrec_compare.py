#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Tests of tools/drawrec_compare.py. Run from the repository root:

    python -m unittest tools/test_drawrec_compare.py
"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import drawrec_compare as compare  # noqa: E402


def frame(number, draw, clear=0, dispatch=0, blit=0, dma_copy=0, blit_mem=0, draw_texture=0):
    return compare.Frame(number, (draw, clear, dispatch, blit, dma_copy, blit_mem, draw_texture),
                         0x0123456789abcdef, 0xfedcba9876543210, 0x0011223344556677)


def sig(value, count, text):
    kind = text.split()[0]
    return compare.Sig(value, count, text, kind)


class ParseFrameTest(unittest.TestCase):

    def test_good_line(self):
        parsed = compare.parse_frame_line(
            '3 1 2 3 4 5 6 7 0123456789abcdef fedcba9876543210 0011223344556677\n')
        self.assertEqual(parsed.number, 3)
        self.assertEqual(parsed.counts, (1, 2, 3, 4, 5, 6, 7))
        self.assertEqual(parsed.ordered, 0x0123456789abcdef)
        self.assertEqual(parsed.summed, 0xfedcba9876543210)
        self.assertEqual(parsed.present, 0x0011223344556677)

    def test_ten_fields(self):
        with self.assertRaises(ValueError):
            compare.parse_frame_line('3 1 2 3 4 5 6 7 0123456789abcdef fedcba9876543210\n')

    def test_twelve_fields(self):
        with self.assertRaises(ValueError):
            compare.parse_frame_line(
                '3 1 2 3 4 5 6 7 0123456789abcdef fedcba9876543210 0011223344556677 0\n')

    def test_bad_count(self):
        with self.assertRaises(ValueError):
            compare.parse_frame_line(
                '3 x 2 3 4 5 6 7 0123456789abcdef fedcba9876543210 0011223344556677\n')

    def test_bad_hex(self):
        with self.assertRaises(ValueError):
            compare.parse_frame_line(
                '3 1 2 3 4 5 6 7 0123456789abcdeg fedcba9876543210 0011223344556677\n')


class ParseSigsTest(unittest.TestCase):

    def test_header(self):
        header = compare.parse_sigs_header('frames 120 dropped 3 shader_uncached 7\n')
        self.assertEqual(header.frames, 120)
        self.assertEqual(header.dropped, 3)
        self.assertEqual(header.shader_uncached, 7)

    def test_bad_header(self):
        with self.assertRaises(ValueError):
            compare.parse_sigs_header('frames 120 dropped 3\n')

    def test_text_with_spaces(self):
        parsed = compare.parse_sig_line(
            '0123456789abcdef 42 draw indexed=1 topo=0x3 count=6 inst=1 sh=0000000000000001\n')
        self.assertEqual(parsed.sig, 0x0123456789abcdef)
        self.assertEqual(parsed.count, 42)
        self.assertEqual(parsed.kind, 'draw')
        self.assertEqual(parsed.text, 'draw indexed=1 topo=0x3 count=6 inst=1 sh=0000000000000001')

    def test_zero_count(self):
        with self.assertRaises(ValueError):
            compare.parse_sig_line('0123456789abcdef 0 draw\n')

    def test_unknown_kind(self):
        with self.assertRaises(ValueError):
            compare.parse_sig_line('0123456789abcdef 1 paint\n')

    def test_repeated_signature(self):
        lines = ['frames 1 dropped 0 shader_uncached 0\n',
                 '%016x 1 draw\n' % 0x10,
                 '%016x 2 draw\n' % 0x10]
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, 'x.sigs.txt')
            with open(path, 'w', encoding='ascii', newline='\n') as record:
                record.writelines(lines)
            with self.assertRaises(SystemExit) as caught:
                compare.load_sigs(path)
        self.assertIn('%s:3:' % path, str(caught.exception))


class StatisticsTest(unittest.TestCase):

    def test_median_min_max(self):
        frames = [frame(1, 1), frame(2, 5), frame(3, 3)]
        stats = dict((row[0], row[1:]) for row in compare.count_statistics(frames, 0))
        self.assertEqual(stats['draw'], (3, 1, 5))

    def test_skip_leaves_out_first_frame(self):
        frames = [frame(1, 1), frame(2, 5), frame(3, 3)]
        stats = dict((row[0], row[1:]) for row in compare.count_statistics(frames, 1))
        self.assertEqual(stats['draw'], (4, 3, 5))

    def test_empty_after_skip(self):
        with self.assertRaises(ValueError):
            compare.count_statistics([frame(1, 1)], 1)


class OneRunOnlyTest(unittest.TestCase):

    def test_selection(self):
        a = [sig(0x1, 2, 'draw a'), sig(0x2, 4, 'clear b')]
        only = compare.one_run_only(a, {0x2})
        self.assertEqual([entry.sig for entry in only], [0x1])

    def test_sorted_by_rate(self):
        entries = [sig(0x1, 1, 'draw a'), sig(0x2, 4, 'draw b'), sig(0x3, 2, 'draw c')]
        rows = compare.rank_by_rate(entries, 10, 30)
        self.assertEqual([entry.sig for entry in rows], [0x2, 0x3, 0x1])

    def test_top(self):
        entries = [sig(0x1, 1, 'draw a'), sig(0x2, 4, 'draw b')]
        self.assertEqual(len(compare.rank_by_rate(entries, 10, 1)), 1)

    def test_zero_frames(self):
        with self.assertRaises(ValueError):
            compare.rank_by_rate([sig(0x1, 1, 'draw a')], 0, 30)


class SharedTest(unittest.TestCase):

    def test_boundaries(self):
        a = [sig(0x1, 10, 'draw a'), sig(0x2, 10, 'draw b'), sig(0x3, 10, 'draw c')]
        b = [sig(0x1, 20, 'draw a'), sig(0x2, 25, 'draw b'), sig(0x3, 4, 'draw c')]
        shared, off = compare.shared_rows(a, b, 10, 10, 2.0)
        self.assertEqual(shared, 3)
        self.assertEqual(sorted(row.entry.sig for row in off), [0x2, 0x3])

    def test_sort_by_absolute_log(self):
        a = [sig(0x1, 10, 'draw a'), sig(0x2, 10, 'draw b')]
        b = [sig(0x1, 3, 'draw a'), sig(0x2, 30, 'draw b')]
        _, off = compare.shared_rows(a, b, 10, 10, 2.0)
        self.assertEqual([row.entry.sig for row in off], [0x1, 0x2])

    def test_zero_frames(self):
        with self.assertRaises(ValueError):
            compare.shared_rows([sig(0x1, 1, 'draw a')], [sig(0x1, 1, 'draw a')], 0, 10, 2.0)


class CoverageTest(unittest.TestCase):

    def test_shared_percent(self):
        entries = [sig(0x1, 6, 'draw shared'), sig(0x2, 2, 'draw only'), sig(0x3, 10, 'present')]
        self.assertEqual(compare.coverage(entries, {0x1}), 75.0)

    def test_no_call(self):
        with self.assertRaises(ValueError):
            compare.coverage([sig(0x3, 10, 'present')], set())


if __name__ == '__main__':
    unittest.main()
