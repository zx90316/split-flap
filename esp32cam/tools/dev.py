#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""NodeMCU ESP-32S build/upload/test entry point. Never arms a motor.

The esp32cam directory is retained so existing scripts and source links keep working.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('mega_dev_shared', ROOT / 'mega2560/tools/dev.py')
shared = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(shared)
FQBN = ('esp32:esp32:esp32:PartitionScheme=min_spiffs,FlashSize=4M,'
        'FlashMode=dio,FlashFreq=40,PSRAM=disabled')
CORE = 'esp32:esp32@3.3.12'
INDEX = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
BUILD = ROOT / 'esp32cam/.build'
DIST = ROOT / 'esp32cam/dist'


def run(args):
    shared.run(args)


def package(image=None, destination=None):
    image = Path(image or BUILD / 'controller.ino.bin')
    destination = Path(destination or DIST)
    binary = image.read_bytes()
    if len(binary) < 256 or len(binary) > 1966080 or binary[0] != 0xE9 or binary[12:14] != b'\0\0':
        raise ValueError('不是適用於 ESP32／min_spiffs 的映像。')
    digest = hashlib.sha256(binary).digest()
    header = b'SFESP01\0' + struct.pack('<II', len(binary), 1) + digest + b'splitflap-32s-v1'
    assert len(header) == 64
    destination.mkdir(parents=True, exist_ok=True)
    result = destination / 'splitflap-esp32s.sffw'
    result.write_bytes(header + binary)
    shutil.copyfile(image, destination / 'splitflap-esp32s.bin')
    metadata = {'device': 'splitflap-esp32s', 'hardware': 'NodeMCU ESP-32S', 'board': FQBN,
                'core': CORE, 'protocol': 1, 'firmware': '1.2.0', 'psram': False,
                'size': len(binary), 'sha256': digest.hex()}
    (destination / 'firmware.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print(f'ESP32-S：網頁更新檔 {result} (映像 {len(binary)} bytes，封裝 {len(header) + len(binary)} bytes，SHA-256 {digest.hex()[:16]}…)')
    return result


def compile_firmware(clean=False):
    run([sys.executable, ROOT / 'esp32cam/tools/embed_web.py'])
    args = [shared.arduino_cli(), 'compile', '--fqbn', FQBN, '--warnings', 'all',
            '--libraries', ROOT / 'libraries', '--build-path', BUILD, '--no-color']
    if clean:
        args.append('--clean')
    run(args + [ROOT / 'esp32cam/controller'])
    package()


def upload(port):
    compile_firmware()
    print('ESP32-S：關閉馬達 12V；拔該板外部 5V 與板間四條 UART 訊號線，再用板上 USB 接電腦。', flush=True)
    print('通常自動進入燒錄；若停在 Connecting，按住 BOOT，連線開始後放開。', flush=True)
    run([shared.arduino_cli(), 'upload', '--fqbn', FQBN, '--port', port, '--input-dir', BUILD,
         '--upload-property', 'upload.speed=115200', '--verify', '--no-color', ROOT / 'esp32cam/controller'])
    print('燒錄完成；若未自動啟動，按 EN／RESET。無需 CP2102 或 GPIO0 跳線。', flush=True)


def monitor(port):
    print('ESP32-S：115200，輸入 ACCESS 顯示 Wi-Fi／登入資料，STATUS 看連線。Ctrl+C 結束。', flush=True)
    run([shared.arduino_cli(), 'monitor', '--port', port, '--config', 'baudrate=115200',
         '--config', 'dtr=off', '--config', 'rts=off', '--no-color'])


def test_web():
    bundled = Path.home() / '.cache/codex-runtimes/codex-primary-runtime/dependencies/node'
    node = os.environ.get('SF_NODE') or shutil.which('node')
    if not node and (bundled / 'bin/node.exe').is_file():
        node = str(bundled / 'bin/node.exe')
    if not node:
        raise FileNotFoundError('手機網頁測試需要 Node.js；一般韌體建置不需要。')
    environment = shared.environment()
    environment['SF_PYTHON'] = sys.executable
    if 'SF_PLAYWRIGHT_MODULE' not in environment and (bundled / 'node_modules/playwright').is_dir():
        environment['SF_PLAYWRIGHT_MODULE'] = str(bundled / 'node_modules/playwright')
    subprocess.run([str(node), str(ROOT / 'esp32cam/test/web.test.cjs')], cwd=ROOT,
                   env=environment, check=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['doctor', 'setup', 'ports', 'build', 'rebuild', 'upload',
                         'upload-monitor', 'monitor', 'test', 'test-web', 'native-debug', 'diagrams', 'mock-web'])
    parser.add_argument('--port')
    args = parser.parse_args(argv)
    try:
        port = shared.validate_port(args.port) if args.action in {'upload', 'upload-monitor', 'monitor'} else None
        if args.action == 'setup':
            run([shared.arduino_cli(), 'core', 'update-index', '--additional-urls', INDEX])
            run([shared.arduino_cli(), 'core', 'install', CORE, '--additional-urls', INDEX])
            run([sys.executable, ROOT / 'esp32cam/test/run.py', '--setup'])
        elif args.action == 'doctor':
            shared.doctor()
            run([shared.arduino_cli(), 'board', 'details', '--fqbn', FQBN, '--no-color'])
        elif args.action == 'ports':
            run([shared.arduino_cli(), 'board', 'list', '--no-color'])
        elif args.action in {'build', 'rebuild'}:
            compile_firmware(args.action == 'rebuild')
        elif args.action in {'upload', 'upload-monitor'}:
            upload(port)
            if args.action == 'upload-monitor':
                monitor(port)
        elif args.action == 'monitor':
            monitor(port)
        elif args.action in {'test', 'native-debug'}:
            if args.action == 'test':
                run([sys.executable, '-m', 'unittest', 'discover', '-s', ROOT / 'esp32cam/test', '-p', 'test_*.py'])
            command = [sys.executable, ROOT / 'esp32cam/test/run.py']
            if args.action == 'native-debug':
                command += ['--debug', '--build-only']
            run(command)
        elif args.action == 'diagrams':
            run([sys.executable, ROOT / 'esp32cam/tools/generate_wiring.py'])
        elif args.action == 'mock-web':
            run([sys.executable, ROOT / 'esp32cam/test/mock_web.py'])
        elif args.action == 'test-web':
            test_web()
        return 0
    except (FileNotFoundError, ValueError) as error:
        print(f'ESP32-S：{error}', file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        print(f'ESP32-S：操作失敗（代碼 {error.returncode}）；後續步驟停止。', file=sys.stderr)
        return error.returncode if error.returncode > 0 else 1
    except KeyboardInterrupt:
        print('\nESP32-S：操作已結束。', flush=True)
        return 130


if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8')
    _process_job = shared.configure_process_lifetime()
    raise SystemExit(main())
