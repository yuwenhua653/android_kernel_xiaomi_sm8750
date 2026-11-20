#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host fault injection of production MCA functions; not a hardware test."""
from pathlib import Path
import re
import unittest
from test_annibale_peripherals import ROOT, function, run

MCA = ROOT / 'drivers/power/supply/mca'
PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint32_t __le32;
#define MCA_GLINK_DATA_MAX 256
#define min(a,b) ((a)<(b)?(a):(b))
#define cpu_to_le32(x) (x)
#define le32_to_cpu(x) (x)
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
'''


class MCATests(unittest.TestCase):
    def test_protocol_and_request_failures(self):
        fixture = Path(__file__).with_name('annibale_mca_glink_fixture.c').read_text()
        for root in (MCA, MCA.with_name('mca-oss')):
            with self.subTest(implementation=root.name):
                source = (root / 'mca_common/mca_adsp_glink.c').read_text()
                protocol = (root / 'mca_common/mca_glink_protocol.h').read_text()
                run(PRELUDE + protocol + fixture.replace('/* PRODUCTION */',
                                                        function(source, 'mca_transfer')))

    def test_restart_event_ordering(self):
        fixture = Path(__file__).with_name('annibale_mca_state_fixture.c').read_text()
        for root in (MCA, MCA.with_name('mca-oss')):
            with self.subTest(implementation=root.name):
                source = (root / 'mca_common/mca_adsp_glink.c').read_text()
                functions = '\n'.join(function(source, name) for name in
                                      ['mca_state_work', 'mca_queue_state', 'mca_state'])
                run(PRELUDE + fixture.replace('/* PRODUCTION */', functions))

    def test_dt_parsing_boundaries(self):
        source = (MCA / 'mca_common/mca_parse_dts.c').read_text()
        source = re.sub(r'^#include.*\n', '', source, flags=re.M)
        source = re.sub(r'^(EXPORT_SYMBOL|MODULE_\w+)\([^\n]*\n', '', source, flags=re.M)
        fixture = Path(__file__).with_name('annibale_mca_dts_fixture.c').read_text()
        run(PRELUDE + fixture.replace('/* PRODUCTION */', source))

    def test_identity_mapping(self):
        source = (MCA / 'mca_common/mca_hwid.c').read_text()
        source = re.sub(r'^#include.*\n', '', source, flags=re.M)
        source = re.sub(r'^(EXPORT_SYMBOL|module_init|MODULE_\w+)\([^\n]*\n', '', source, flags=re.M)
        header = (MCA / 'include/mca/common/mca_hwid.h').read_text()
        header = re.sub(r'^#include.*\n', '', header, flags=re.M)
        fixture = Path(__file__).with_name('annibale_mca_hwid_fixture.c').read_text()
        run(PRELUDE + header + fixture.replace('/* PRODUCTION */', source))


if __name__ == '__main__':
    unittest.main()
