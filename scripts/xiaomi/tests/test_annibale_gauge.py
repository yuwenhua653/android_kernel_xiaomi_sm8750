#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run production gauge and MCA registry code with a fault-injected SMBus."""
from pathlib import Path
import unittest
from test_annibale_mca_services import BASE, MCA, clean, extract, run_service


class GaugeTests(unittest.TestCase):
    def test_readout_probe_pm_and_teardown(self):
        source = clean((MCA / 'mca_bq27z561.c').read_text())
        core = source[:source.index('struct annibale_fg_attribute')]
        lifecycle = '\n'.join(extract(source, name) for name in [
            'annibale_fg_unpublish', 'annibale_fg_probe', 'annibale_fg_remove',
            'annibale_fg_suspend', 'annibale_fg_resume'])
        registry = clean((MCA / 'mca_platform/mca_platform_fg_ic_ops.c').read_text())
        registry = registry[:registry.index('/*\n * The registry above')]
        header = clean((MCA / 'include/mca/platform/platform_fg_ic_ops.h').read_text())
        fixture = Path(__file__).with_name('annibale_gauge_fixture.c').read_text()
        run_service(BASE + header + fixture.replace('/* PRODUCTION */',
                                                   core + registry + lifecycle))


if __name__ == '__main__':
    unittest.main()
