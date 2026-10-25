#!/usr/bin/env python3
"""Run host behavioral tests. Requires C++11 compiler, or --zig /path/to/zig."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
build = root / 'test' / '.build'
build.mkdir(exist_ok=True)
if args.zig:
    compiler = [str(args.zig.resolve()), 'c++']
else:
    compiler = shlex.split(os.environ.get('CXX', shutil.which('g++') or shutil.which('clang++') or ''))
if not compiler:
    parser.error('Install a C++11 compiler or provide --zig PATH.')
env = os.environ.copy()
scratch = build / 'tmp'
scratch.mkdir(exist_ok=True)
env['TEMP'] = env['TMP'] = env['TMPDIR'] = str(scratch)
env.setdefault('ZIG_LOCAL_CACHE_DIR', str(build / 'zig-local'))
env.setdefault('ZIG_GLOBAL_CACHE_DIR', str(build / 'zig-global'))
for suite in ['test_bench', 'test_adapter']:
    binary = build / (suite + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(compiler + ['-std=c++11', '-Wall', '-Wextra', '-Werror',
        '-I' + str(root / 'unit_test'), '-I' + str(root / 'test' / 'stubs'),
        str(root / 'test' / (suite + '.cpp')), str(root / 'unit_test' / 'bench.cpp'),
        '-o', str(binary)], check=True, env=env)
    subprocess.run([str(binary)], check=True)
