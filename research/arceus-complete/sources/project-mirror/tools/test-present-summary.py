# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('summary',Path(__file__).with_name('summarize-present.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def row(t,sc='1',result=0):
    return dict(begin_ns=t,end_ns=t+100,swapchain=sc,result=result)
class SummaryTests(unittest.TestCase):
    def test_streams_and_errors(self):
        s=m.summarize([row(0),row(0,'2'),row(10000000,'2'),row(33333333),row(40000000,result=-1)],0)
        self.assertEqual(s['failed_calls'],1)
        self.assertAlmostEqual(s['swapchains']['1']['mean_interval_ms'],33.333333)
        self.assertEqual(s['swapchains']['2']['mean_interval_ms'],10)
    def test_warmup(self):
        s=m.summarize([row(0),row(1000000000),row(1033333333)],1)
        self.assertEqual(s['swapchains']['1']['sampled_calls'],2)
    def test_reversed_clock(self):
        with self.assertRaises(ValueError): m.summarize([row(100),row(50)],0)
    def test_empty(self):
        self.assertEqual(m.summarize([])['swapchains'],{})
if __name__=='__main__': unittest.main()
