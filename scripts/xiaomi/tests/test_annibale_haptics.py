#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host fault-injection checks against extracted, unmodified driver functions.

Only kernel primitives and bus/PM calls are modeled. This is not a kernel,
locking stress test, hardware emulator, or substitute for a device test.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "drivers/input/misc/qcom-hv-haptics.c"


def function(source, name):
    match = re.search(r"^static int " + re.escape(name) + r"\(", source, re.M)
    if match is None:
        raise AssertionError(f"Missing production function: {name}")
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


class HapticsControlTests(unittest.TestCase):
    def test_production_controls_with_bus_and_pm_failures(self):
        source = SOURCE.read_text()
        constants = "\n".join(re.findall(
            r"^#define (?:TLRA_STEP_US|TLRA_MAX_US|TLRA_OL_MSB_MASK|TLRA_OL_LSB_MASK|"
            r"HAP_CFG_TLRA_OL_HIGH_REG|HAP_CFG_SPMI_PLAY_REG|PLAY_EN_BIT)\s+[^\n]+", source, re.M))
        names = ["haptics_masked_write", "haptics_config_openloop_lra_period",
                 "haptics_xiaomi_f0_to_period", "haptics_xiaomi_set_f0",
                 "haptics_xiaomi_set_vmax", "haptics_enable_play", "swr_slave_reg_enable"]
        functions = "\n\n".join(function(source, name) for name in names)
        fixture = Path(__file__).with_name("annibale_haptics_fixture.c").read_text()
        code = fixture.replace("/* PRODUCTION_CONSTANTS */", constants).replace(
            "/* PRODUCTION_FUNCTIONS */", functions)
        with tempfile.TemporaryDirectory(prefix="annibale-haptics-test-") as directory:
            c_file = Path(directory) / "test.c"
            exe = Path(directory) / "test"
            c_file.write_text(code)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-fno-sanitize-recover=all",
                            str(c_file), "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertIn("PASS: 30 haptics control cases", result.stdout)
            print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
