#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Production-function fault checks; not a phone or Android ROM test."""
from pathlib import Path
import re
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).parent


def function(source, name):
    match = re.search(r'^.*\b' + name + r'\([^;]*?\)\n\{', source, re.M)
    if not match:
        raise AssertionError(name)
    pos = source.index('{', match.start())
    depth, end = 1, pos + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def run(code):
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        (root/'test.c').write_text(code)
        subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-sign-compare',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-pthread', str(root/'test.c'), '-o', str(root/'test')], check=True)
        # This executor uses ptrace; LSan cannot attach. ASan/UBSan remain enabled.
        subprocess.run([str(root/'test')], check=True, timeout=30,
                       env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))


class DualRomKernelTests(unittest.TestCase):
    def test_miev_event_format_bounds_and_errors(self):
        source = (ROOT/'drivers/staging/miev/mievent.c').read_text()
        source = re.sub(r'^#include.*$', '', source, flags=re.M)
        header = (ROOT/'include/miev/mievent.h').read_text()
        event = re.search(r'struct misight_mievent \{.*?\};', header, re.S)[0]
        fixture = (HERE/'annibale_miev_event_fixture.c').read_text()
        run(fixture.replace('/* EVENT */', event).replace('/* PRODUCTION */', source))

    def test_miev_no_reader_short_reads_and_work_ownership(self):
        source = (ROOT/'drivers/staging/miev/midevice.c').read_text()
        prefix = source[source.index('#define FIFO_SIZE'):source.index('static int miev_open')]
        code = prefix + '\n' + function(source, 'miev_read')
        fixture = (HERE/'annibale_miev_queue_fixture.c').read_text()
        run(fixture.replace('/* PRODUCTION */', code))

    def test_reclaim_admission_references_and_rmap_boundaries(self):
        slab = (ROOT/'drivers/xiaomi/kshrink_slabd/kshrink_slabd.c').read_text()
        rmap = (ROOT/'drivers/staging/mi_rmap_efficiency/mi_rmap_efficiency.c').read_text()
        worker = function(slab, 'kshrink_slabd_func')
        slab = slab[slab.index('static struct task_struct *shrink_slabd_tsk'):slab.index('static void set_async_slabd_cpus')] + '\n' + worker
        rmap = rmap[rmap.index('enum mi_folio_references'):rmap.index('static ssize_t show_mi_mapcount_thres')]
        fixture = (HERE/'annibale_reclaim_fixture.c').read_text()
        run(fixture.replace('/* PRODUCTION */', slab + '\n' + rmap))

    def test_mca_event_validation_rate_limits_and_allocation_failure(self):
        base = ROOT/'drivers/power/supply/mca-oss'
        header = (base/'include/mca/common/mca_charge_mievent.h').read_text()
        header = re.sub(r'^#include.*$', '', header, flags=re.M)
        source = (base/'mca_common/mca_charge_mievent.c').read_text()
        source = source[source.index('static struct charge_mievent_info'):source.index('\n#else\nvoid mca_charge_mievent_report')]
        fixture = (HERE/'annibale_mca_miev_fixture.c').read_text()
        run(fixture.replace('/* CONTRACT */', header).replace('/* PRODUCTION */', source))

    def test_ntsync_timeout_clock_and_wakeup(self):
        source = (ROOT/'drivers/misc/ntsync.c').read_text()
        fixture = (HERE/'annibale_ntsync_fixture.c').read_text()
        run(fixture.replace('/* PRODUCTION */', function(source, 'ntsync_schedule')))


if __name__ == '__main__':
    unittest.main()
