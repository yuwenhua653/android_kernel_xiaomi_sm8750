#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run actual full-MCA functions with host fault injection, not kernel/ROM tests."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
MCA = ROOT / 'drivers/power/supply/mca-oss'
HERE = Path(__file__).resolve().parent
BASE = (HERE / 'annibale_r52_host.h').read_text()


def extract(text, name):
    pattern = r'^(?:static\s+)?(?:inline\s+)?[\w *]+\b' + name + r'\([^;]*?\)\s*\n\{'
    match = re.search(pattern, text, re.M)
    if not match:
        raise AssertionError(name)
    pos, depth = match.end(), 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[match.start():pos]


def compile_run(source):
    env = dict(os.environ)
    sanitize = os.environ.get('MCA_TEST_SANITIZE', '1') != '0'
    with tempfile.TemporaryDirectory(prefix='annibale-r52-') as td:
        directory = Path(td)
        src = directory / 'test.c'
        src.write_text(source)
        if os.name == 'nt':
            vcvars = Path(os.environ.get('MCA_VCVARS',
                r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'))
            # Capture the developer shell privately; never print its environment.
            env_script = directory / 'compiler-env.cmd'
            env_script.write_text(f'@call "{vcvars}" >nul\n@if errorlevel 1 exit /b 1\n@set\n')
            shell = subprocess.check_output(['cmd.exe', '/d', '/c', str(env_script)], text=True)
            for line in shell.splitlines():
                if '=' in line and not line.startswith('='):
                    key, value = line.split('=', 1)
                    if key.upper() in ('PATH', 'INCLUDE', 'LIB', 'LIBPATH'):
                        for old in list(env):
                            if old.upper() == key.upper():
                                del env[old]
                        env[key.upper()] = value
            exe = directory / 'test.exe'
            compiler = shutil.which('cl.exe', path=env['PATH'])
            if not compiler:
                raise AssertionError('MSVC compiler unavailable after vcvars64')
            command = [compiler, '/nologo', '/std:c11', '/W3', '/WX', '/Zi']
            command += ['/fsanitize=address'] if sanitize else ['/RTC1']
            command += [str(src), '/Fe:' + str(exe)]
        else:
            exe = directory / 'test'
            command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-function', '-Wno-unused-parameter', '-pthread']
            if sanitize:
                command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
            command += [str(src), '-o', str(exe)]
        built = subprocess.run(command, cwd=directory, env=env, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if built.returncode:
            raise AssertionError(built.stdout)
        run = subprocess.run([str(exe)], cwd=directory, env=env, text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        if run.returncode:
            raise AssertionError(run.stdout)
        print(run.stdout, end='')


class MCAFullServices(unittest.TestCase):
    def test_sysfs_ownership_and_failure_cleanup(self):
        src = (MCA / 'mca_common/mca_sysfs.c').read_text()
        src = re.sub(r'^#include.*\n', '', src, flags=re.M)
        src = src[:src.index('static struct dentry *g_mca_dbg_dir;')]
        fixture = (HERE / 'annibale_r52_sysfs_fixture.c').read_text()
        compile_run(BASE + fixture.replace('/* PRODUCTION */', src))

    def test_sysfs_link_and_file_rollback(self):
        src = (MCA / 'mca_common/mca_sysfs.c').read_text()
        names = ['mca_sysfs_create_link_group', 'mca_sysfs_remove_link_group',
                 'mca_sysfs_create_files', 'mca_sysfs_remove_files',
                 'mca_sysfs_init_attrs', 'mca_sysfs_lookup_attr']
        code = '\n'.join(extract(src, n) for n in names)
        fixture = (HERE / 'annibale_mca_sysfs_fixture.c').read_text()
        compile_run(BASE + fixture.replace('/* PRODUCTION */', code))

    def test_charge_log_bounds_and_unregister(self):
        src = (MCA / 'mca_common/mca_log.c').read_text()
        names = ['mca_log_charge_log_register', 'mca_log_charge_log_unregister',
                 'mca_log_charge_log_dump', 'mca_log_charge_log_dump_log_head',
                 'mca_log_charge_log_dump_log_context']
        code = '\n'.join(extract(src, n) for n in names)
        header = (MCA / 'include/mca/common/mca_log.h').read_text()
        types = header[header.index('enum mca_charge_log_id_ele'):header.index('__printf')]
        registry = src[src.index('struct mca_charge_log_ops_data {'):src.index('enum mca_log_sysfs_type')]
        fixture = (HERE / 'annibale_r52_log_fixture.c').read_text()
        compile_run(BASE + types + registry + fixture.replace('/* PRODUCTION */', code))

    def test_log_buffer_reads_and_index_snapshot(self):
        src = (MCA / 'mca_common/mca_log.c').read_text()
        defines = '\n'.join(line for line in src.splitlines() if line.startswith('#define MCA_LOG_BUFF')) + '\n'
        types = src[src.index('struct mca_log_buf_info {'):src.index('struct mca_cur_time')]
        types += src[src.index('enum mca_log_sysfs_type {'):src.index('static struct mca_log_buf_info g_mca_log_info;')]
        code = '\n'.join(extract(src, n) for n in ['mca_log_sysfs_store', 'mca_log_sysfs_show'])
        fixture = (HERE / 'annibale_r52_log_buffer_fixture.c').read_text()
        compile_run(BASE + defines + types + fixture.replace('/* PRODUCTION */', code))


if __name__ == '__main__':
    unittest.main()
