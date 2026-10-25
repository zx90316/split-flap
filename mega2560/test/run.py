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
parser.add_argument('--debug', action='store_true', help='Build with symbols and no optimization.')
parser.add_argument('--build-only', action='store_true', help='Build without running the test binary.')
parser.add_argument('--suite', choices=['bench', 'adapter'], help='Build only one suite.')
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
if args.debug and os.name == 'nt' and not args.zig:
    parser.error('Windows cppvsdbg requires PDB symbols; provide --zig PATH.')
env = os.environ.copy()
scratch = build / 'tmp'
scratch.mkdir(exist_ok=True)
env['TEMP'] = env['TMP'] = env['TMPDIR'] = str(scratch)
env.setdefault('ZIG_LOCAL_CACHE_DIR', str(build / 'zig-local'))
env.setdefault('ZIG_GLOBAL_CACHE_DIR', str(build / 'zig-global'))
debug_flags = ['-g', '-O0'] if args.debug else []
if args.debug and os.name == 'nt':
    debug_flags += ['-gcodeview']
suites = ['test_' + args.suite] if args.suite else ['test_bench', 'test_adapter']
for suite in suites:
    suffix = '_debug' if args.debug else ''
    binary = build / (suite + suffix + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(compiler + ['-std=c++11', '-Wall', '-Wextra', '-Werror',
        '-I' + str(root / 'unit_test'), '-I' + str(root / 'test' / 'stubs'),
        str(root / 'test' / (suite + '.cpp')), str(root / 'unit_test' / 'bench.cpp'),
        '-o', str(binary)] + debug_flags, check=True, env=env)
    if not args.build_only:
        subprocess.run([str(binary)], check=True)
    else:
        print('Built:', binary, flush=True)
