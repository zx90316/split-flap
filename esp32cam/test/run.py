#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Native end-to-end bridge/display/API tests. --setup fetches pinned test-only cJSON."""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('mega_dev_native', ROOT / 'mega2560/tools/dev.py')
shared = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(shared)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--setup', action='store_true')
parser.add_argument('--debug', action='store_true')
parser.add_argument('--build-only', action='store_true')
args = parser.parse_args()
dep = ROOT / '.tools/cjson-1.7.19'
HASHES = {'cJSON.c': '298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d',
          'cJSON.h': '25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee'}
if args.setup:
    dep.mkdir(exist_ok=True)
    for name in ['cJSON.c', 'cJSON.h']:
        data = urllib.request.urlopen(f'https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.19/{name}', timeout=60).read()
        if hashlib.sha256(data).hexdigest() != HASHES[name]:
            parser.error('cJSON source checksum mismatch.')
        (dep / name).write_bytes(data)
        print(name, hashlib.sha256(data).hexdigest())
    raise SystemExit(0)
if not (dep / 'cJSON.c').is_file():
    parser.error('Run python esp32cam/test/run.py --setup once (test-only cJSON 1.7.19).')
for name, checksum in HASHES.items():
    if hashlib.sha256((dep / name).read_bytes()).hexdigest() != checksum:
        parser.error('Test dependency checksum mismatch; run --setup.')
zig = shared.zig_compiler()
if not zig:
    parser.error('Run MEGA: 準備主機偵錯工具 first (Zig).')
build = ROOT / 'esp32cam/test/.build'
scratch = build / 'tmp'
scratch.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['TEMP'] = env['TMP'] = env['TMPDIR'] = str(scratch)
env['ZIG_LOCAL_CACHE_DIR'] = str(build / 'zig-local')
env['ZIG_GLOBAL_CACHE_DIR'] = str(build / 'zig-global')
obj = build / 'cJSON.o'
def compile_checked(command):
    result = subprocess.run(command, env=env, capture_output=True)
    if result.stderr:
        (build / 'compiler.log').write_bytes(result.stderr)
    if result.returncode:
        sys.stderr.write(result.stderr.decode('utf-8', errors='replace')[-20000:])
        raise SystemExit(result.returncode)


compile_checked([zig, 'cc', '-std=c99', '-c', dep / 'cJSON.c', '-o', obj])
debug = ['-g', '-O0'] + (['-gcodeview'] if os.name == 'nt' else []) if args.debug else []
binary = build / ('test_bridge' + ('_debug' if args.debug else '') + ('.exe' if os.name == 'nt' else ''))
compile_checked([zig, 'c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
    '-I' + str(ROOT / 'libraries/SplitFlapLink/src'), '-I' + str(ROOT / 'mega2560/test/stubs'),
    '-I' + str(ROOT / 'mega2560/unit_test'), '-I' + str(dep),
    ROOT / 'esp32cam/test/test_bridge.cpp', ROOT / 'mega2560/unit_test/bench.cpp',
    ROOT / 'mega2560/unit_test/panel.cpp', obj, '-o', binary] + debug)
if not args.build_only:
    subprocess.run([binary], check=True, env=env)
else:
    print('Built:', binary)
