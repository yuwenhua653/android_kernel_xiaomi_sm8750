#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Production MCA service functions with host fault injection; no hardware claim."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from test_annibale_mca import ROOT, MCA, PRELUDE


def clean(text):
    return re.sub(r'^#include.*\n', '', text, flags=re.M)


def extract(text, name):
    m = re.search(r'^(?:static )?(?:[\w]+[ *]+)+' + name + r'\(', text, re.M)
    if not m:
        raise AssertionError(name)
    p = text.index('{', m.start())
    end, depth = p + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[m.start():end]


def run_service(code):
    with tempfile.TemporaryDirectory() as td:
        p = Path(td)
        (p / 'test.c').write_text(code)
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-Wno-unused-variable', '-pthread',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        str(p / 'test.c'), '-o', str(p / 'test')], check=True)
        print(subprocess.check_output([str(p / 'test')], text=True, timeout=10), end='')


BASE = PRELUDE + r'''
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <stdarg.h>
#include <time.h>
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define ARRAY_SIZE(x) ((int)(sizeof(x)/sizeof((x)[0])))
#define DECLARE_RWSEM(x) pthread_rwlock_t x = PTHREAD_RWLOCK_INITIALIZER
static void down_read(pthread_rwlock_t *l) { assert(!pthread_rwlock_rdlock(l)); }
static void up_read(pthread_rwlock_t *l) { assert(!pthread_rwlock_unlock(l)); }
static void down_write(pthread_rwlock_t *l) { assert(!pthread_rwlock_wrlock(l)); }
static void up_write(pthread_rwlock_t *l) { assert(!pthread_rwlock_unlock(l)); }
'''


class MCAServiceTests(unittest.TestCase):
    def test_fuel_gauge_registry_and_lifetime(self):
        src = clean((MCA / 'mca_platform/mca_platform_fg_ic_ops.c').read_text())
        src = src[:src.index('/*\n * The registry above')]
        header = clean((MCA / 'include/mca/platform/platform_fg_ic_ops.h').read_text())
        fixture = Path(__file__).with_name('annibale_mca_fg_fixture.c').read_text()
        run_service(BASE + header + fixture.replace('/* PRODUCTION */', src))

    def test_charge_control_failures(self):
        src = clean((MCA / 'mca_common/mca_charge_interface.c').read_text())
        src = src[:src.index('static ssize_t mca_charge_show')]
        header = clean((MCA / 'include/mca/common/mca_charge_interface.h').read_text())
        fixture = Path(__file__).with_name('annibale_mca_charge_fixture.c').read_text()
        run_service(BASE + header + fixture.replace('/* PRODUCTION */', src))

    def test_sysfs_rollback_and_references(self):
        src = (MCA / 'mca_common/mca_sysfs.c').read_text()
        names = ['mca_sysfs_create_link_group', 'mca_sysfs_remove_link_group',
                 'mca_sysfs_create_files', 'mca_sysfs_remove_files',
                 'mca_sysfs_init_attrs', 'mca_sysfs_lookup_attr']
        funcs = '\n'.join(extract(src, name) for name in names)
        fixture = Path(__file__).with_name('annibale_mca_sysfs_fixture.c').read_text()
        run_service(PRELUDE + '#include <sys/types.h>\n' + fixture.replace('/* PRODUCTION */', funcs))

    def test_event_validation_and_send_errors(self):
        src = clean((MCA / 'mca_common/mca_event.c').read_text())
        src = src[:src.index('static DEVICE_ATTR')]
        header = clean((MCA / 'include/mca/common/mca_event.h').read_text())
        fixture = Path(__file__).with_name('annibale_mca_event_fixture.c').read_text()
        run_service(PRELUDE + '#include <sys/types.h>\n#include <ctype.h>\n' +
                    '#define EXPORT_SYMBOL(x)\nstruct notifier_block;\n' + header + fixture.replace('/* PRODUCTION */', src))


if __name__ == '__main__':
    unittest.main()
