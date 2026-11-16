#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Production battery bridge + real MCA registry, with host fault injection."""
from pathlib import Path
import unittest
from test_annibale_mca_services import BASE, MCA, clean, run_service


class BatteryTests(unittest.TestCase):
    def test_measurements_notifications_pm_and_remove(self):
        source = clean((MCA / 'mca_battery.c').read_text())
        source = source[:source.index('static DEFINE_SIMPLE_DEV_PM_OPS')]
        registry = clean((MCA / 'mca_platform/mca_platform_fg_ic_ops.c').read_text())
        registry = registry[:registry.index('/*\n * The registry above')]
        header = clean((MCA / 'include/mca/platform/platform_fg_ic_ops.h').read_text())
        fixture = Path(__file__).with_name('annibale_battery_fixture.c').read_text()
        run_service(BASE + header + fixture.replace('/* PRODUCTION */', registry + source))


if __name__ == '__main__':
    unittest.main()
