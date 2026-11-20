#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Fault injection for the real FSA switch path; no hardware access."""
from pathlib import Path
import re
import unittest
from test_annibale_peripherals import ROOT, function, run


class FsaTests(unittest.TestCase):
    def test_i2c_errors_and_device_reference_release(self):
        source = (ROOT / 'drivers/soc/qcom/fsa4480-i2c.c').read_text()
        header = (ROOT / 'include/linux/soc/qcom/fsa4480-i2c.h').read_text()
        enum = re.search(r'enum fsa_function \{.*?\};', header, re.S)[0]
        defines = '\n'.join(re.findall(
            r'^#define FSA4480_SWITCH_(?:CONTROL|SETTINGS)\s+.*$', source, re.M))
        code = '\n'.join(function(source, name) for name in
                         ('fsa4480_usbc_update_settings', 'fsa4480_switch_event'))
        fixture = Path(__file__).with_name('annibale_fsa_fixture.c').read_text()
        run(fixture.replace('/* CONTRACT */', enum + '\n' + defines)
            .replace('/* PRODUCTION */', code))


if __name__ == '__main__':
    unittest.main()
