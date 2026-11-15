#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run real port functions with host fault injection; not kernel/hardware tests."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


def function(source, name):
    match = re.search(r"^(?:static )?(?:int|void|irqreturn_t) " + name + r"\(", source, re.M)
    if not match:
        raise AssertionError(name)
    pos = source.index('{', match.start())
    depth, end = 1, pos + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def run(code):
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp)
        (p / 'test.c').write_text(code)
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-Wno-unused-variable',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        str(p / 'test.c'), '-o', str(p / 'test')], check=True)
        print(subprocess.check_output([str(p / 'test')], text=True), end='')


class PeripheralTests(unittest.TestCase):
    def test_hwid_missing_and_supplied_identity(self):
        source = (ROOT / 'drivers/misc/hwid/hwid.c').read_text()
        source = source[:source.index('static int __init hwid_init')]
        source = re.sub(r'^#include.*\n', '', source, flags=re.M)
        source = re.sub(r'^(?:module_param\w*|__MODULE_PARM_TYPE|MODULE_PARM_DESC|EXPORT_SYMBOL\w*)\([^\n]*\n', '', source, flags=re.M)
        fixture = Path(__file__).with_name('annibale_hwid_fixture.c').read_text()
        run(fixture.replace('/* PRODUCTION */', source))

    def test_fingerprint_power_failures(self):
        source = (ROOT / 'drivers/input/fingerprint/mi_fp/fp_platform.c').read_text()
        funcs = '\n'.join(function(source, x) for x in ['fp_power_on', 'fp_power_off'])
        fixture = Path(__file__).with_name('annibale_fp_power_fixture.c').read_text()
        run(fixture.replace('/* PRODUCTION */', funcs))

    def test_fingerprint_touch_slot_bounds(self):
        source = (ROOT / 'drivers/input/fingerprint/mi_fp/fp_input.c').read_text()
        fixture = Path(__file__).with_name('annibale_fp_touch_fixture.c').read_text()
        run(fixture.replace('/* PRODUCTION */', function(source, 'xiaomifp_input_event')))

    def test_wifi_publication_and_wake_order(self):
        source = (ROOT / 'drivers/misc/wifi_gpio/xiaomi_wifi_gpio.c').read_text()
        funcs = '\n'.join(function(source, x) for x in ['sar_publish', 'sar_wpas_ready',
                         'wifi_gpio_work', 'wifi_gpio_irq', 'wifi_gpio_remove'])
        fixture = Path(__file__).with_name('annibale_wifi_fixture.c').read_text()
        run(fixture.replace('/* PRODUCTION */', funcs))


if __name__ == '__main__':
    unittest.main()
