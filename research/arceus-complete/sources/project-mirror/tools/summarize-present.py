# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Summarize SNX_PRESENT_TRACE submissions, never display FPS. Standard library only."""
import argparse
import csv
import json
import math
import statistics
from collections import defaultdict


def summarize(rows, warmup_seconds=10):
    groups = defaultdict(list)
    errors = 0
    for row in rows:
        begin, end = int(row['begin_ns']), int(row['end_ns'])
        if end < begin:
            raise ValueError('present completion precedes its start')
        if int(row['result']) not in (0, 1000001003):
            errors += 1
            continue
        groups[row['swapchain']].append((begin, end))
    result = {'measurement': 'Vulkan API submission, not display completion', 'failed_calls': errors, 'swapchains': {}}
    for swapchain, calls in groups.items():
        if any(b[0] <= a[0] for a, b in zip(calls, calls[1:])):
            raise ValueError('non-increasing timestamps in one swapchain')
        cutoff = calls[0][0] + warmup_seconds * 1e9
        samples = [x for x in calls if x[0] >= cutoff]
        intervals = [(b[0] - a[0]) / 1e6 for a, b in zip(samples, samples[1:])]
        data = {'accepted_calls': len(calls), 'sampled_calls': len(samples)}
        if intervals:
            data.update(mean_interval_ms=statistics.mean(intervals), median_interval_ms=statistics.median(intervals),
                        p95_interval_ms=sorted(intervals)[math.ceil(.95 * len(intervals)) - 1],
                        mean_api_duration_ms=statistics.mean((end-begin)/1e6 for begin,end in samples))
        result['swapchains'][swapchain] = data
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('csv')
    p.add_argument('--warmup', type=float, default=10)
    args = p.parse_args()
    if args.warmup < 0:
        p.error('--warmup must be nonnegative')
    with open(args.csv, newline='', encoding='utf-8-sig') as file:
        print(json.dumps(summarize(csv.DictReader(file), args.warmup), indent=2))
