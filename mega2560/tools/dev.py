#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""VS Code entry point. Build/test never opens the serial port or drives a motor."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FQBN = 'arduino:avr:mega:cpu=atmega2560'
CORE = 'arduino:avr@1.8.8'


def configure_process_lifetime():
    """Close tool descendants when VS Code's Stop button terminates this runner."""
    if os.name != 'nt':
        return
    import ctypes
    from ctypes import wintypes

    class Limits(ctypes.Structure):
        _fields_ = [('processTime', ctypes.c_int64), ('jobTime', ctypes.c_int64),
            ('flags', wintypes.DWORD), ('minWorkingSet', ctypes.c_size_t),
            ('maxWorkingSet', ctypes.c_size_t), ('activeProcesses', wintypes.DWORD),
            ('affinity', ctypes.c_size_t), ('priority', wintypes.DWORD),
            ('scheduling', wintypes.DWORD)]

    class JobInfo(ctypes.Structure):
        _fields_ = [('limits', Limits), ('ioCounters', ctypes.c_uint64 * 6),
            ('processMemory', ctypes.c_size_t), ('jobMemory', ctypes.c_size_t),
            ('peakProcessMemory', ctypes.c_size_t), ('peakJobMemory', ctypes.c_size_t)]

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    kernel.CreateJobObjectW.restype = wintypes.HANDLE
    kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    info = JobInfo()
    info.limits.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    handle = kernel.CreateJobObjectW(None, None)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    if not kernel.SetInformationJobObject(handle, 9, ctypes.byref(info), ctypes.sizeof(info)):
        error = ctypes.get_last_error()
        kernel.CloseHandle(handle)
        raise ctypes.WinError(error)
    if not kernel.AssignProcessToJobObject(handle, kernel.GetCurrentProcess()):
        error = ctypes.get_last_error()
        kernel.CloseHandle(handle)
        raise ctypes.WinError(error)
    # Keep the non-inheritable handle until Windows closes it on process exit.
    # The child Arduino CLI and its serial-monitor process inherit job membership.
    return handle


def project_path(relative):
    path = (ROOT / relative).resolve()
    if not path.is_relative_to(ROOT):
        raise ValueError(f'輸出目錄必須位於本專案內：{relative}')
    return path


def environment():
    env = os.environ.copy()
    # Do not depend on the old ignored YAML's absolute D: drive paths.
    for key, relative in {
        'ARDUINO_DIRECTORIES_DATA': '.tools/arduino-data',
        'ARDUINO_DIRECTORIES_DOWNLOADS': '.tools/arduino-downloads',
        'ARDUINO_DIRECTORIES_USER': '.tools/arduino-user',
    }.items():
        env[key] = str(project_path(relative))
    env['ARDUINO_UPDATER_ENABLE_NOTIFICATION'] = 'false'
    env['PYTHONUTF8'] = '1'
    env['PYTHONUNBUFFERED'] = '1'
    return env


def arduino_cli():
    override = os.environ.get('MEGA_ARDUINO_CLI')
    local = project_path('.tools/arduino/arduino-cli.exe' if os.name == 'nt' else '.tools/arduino/arduino-cli')
    candidates = [override, str(local), shutil.which('arduino-cli')]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(Path(candidate).resolve())
    raise FileNotFoundError('找不到 Arduino CLI。請依 mega2560/vscode.md 安裝，或設定 MEGA_ARDUINO_CLI。')


def zig_compiler():
    local = project_path('.tools/host-compiler/ziglang/zig.exe' if os.name == 'nt' else '.tools/host-compiler/ziglang/zig')
    if local.is_file():
        return str(local)
    return shutil.which('zig')


def run(arguments):
    subprocess.run([str(arg) for arg in arguments], cwd=ROOT, env=environment(), check=True)


def compile_firmware(clean=False):
    print('MEGA：建置正式韌體；不開啟 COM 埠。', flush=True)
    args = [arduino_cli(), 'compile', '--fqbn', FQBN, '--warnings', 'all',
            '--libraries', ROOT / 'libraries',
            '--build-path', project_path('mega2560/.build'), '--no-color']
    if clean:
        args.append('--clean')
    run(args + [ROOT / 'mega2560/unit_test'])


def validate_port(port):
    if not port or not port.strip():
        raise ValueError('請指定已確認的 Mega 連接埠，例如 --port COM3。')
    port = port.strip()
    if os.name == 'nt':
        if not re.fullmatch(r'COM[1-9][0-9]*', port, re.IGNORECASE):
            raise ValueError('Windows 連接埠格式為 COM3、COM4 等；請先執行查詢連接埠。')
        return port.upper()
    if not port.startswith('/dev/'):
        raise ValueError('請指定 /dev/ 下已確認的序列裝置。')
    return port


def upload(port):
    # A failed compile raises before upload. Never upload a stale binary.
    compile_firmware()
    print(f'MEGA：寫入並驗證 {port}；請保持馬達 12V 分支關閉。', flush=True)
    run([arduino_cli(), 'upload', '--fqbn', FQBN, '--port', port,
         '--input-dir', project_path('mega2560/.build'), '--verify', '--no-color',
         ROOT / 'mega2560/unit_test'])


def monitor(port):
    print('MEGA：115200 串列監控；每行 Enter 送出，Ctrl+C 結束。', flush=True)
    print('輸入 STATUS 看狀態、WATCH 看霍爾；WATCH 中輸入 x 加 Enter 退出。', flush=True)
    print('馬達不會自動啟用；要試轉時依調機指南操作 ARM／STEP。', flush=True)
    run([arduino_cli(), 'monitor', '--port', port, '--config', 'baudrate=115200', '--no-color'])


def host_tests(debug=False, suite=None):
    args = [sys.executable, ROOT / 'mega2560/test/run.py']
    zig = zig_compiler()
    if zig:
        args += ['--zig', zig]
    if debug:
        if os.name == 'nt' and not zig:
            raise FileNotFoundError('Windows 主機中斷點需 Zig；請先執行「MEGA: 準備主機偵錯工具」。')
        args += ['--debug', '--build-only', '--suite', suite]
    print('MEGA：主機模擬，不連接 Mega、不驅動馬達。', flush=True)
    run(args)


def doctor():
    print(f'Python: {sys.executable}', flush=True)
    print(f'專案: {ROOT}', flush=True)
    print(f'Arduino CLI: {arduino_cli()}', flush=True)
    run([arduino_cli(), 'version'])
    run([arduino_cli(), 'core', 'list', '--no-color'])
    zig = zig_compiler()
    print(f'主機偵錯編譯器: {zig or "未安裝；執行準備主機偵錯工具"}', flush=True)
    if zig:
        run([zig, 'version'])
    print('實板：USB 燒錄及串列除錯；主機：C++ 中斷點與變數檢視。', flush=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['doctor', 'setup', 'setup-host', 'ports', 'build',
        'rebuild', 'upload', 'monitor', 'upload-monitor', 'test', 'native-debug', 'diagrams'])
    parser.add_argument('--port')
    parser.add_argument('--suite', choices=['bench', 'adapter', 'panel'], default='bench')
    args = parser.parse_args(argv)
    try:
        port = validate_port(args.port) if args.action in {'upload', 'monitor', 'upload-monitor'} else None
        if args.action == 'doctor':
            doctor()
        elif args.action == 'setup':
            print('MEGA：安裝固定版本 AVR 核心至本專案 .tools/（需網路）。', flush=True)
            run([arduino_cli(), 'core', 'update-index'])
            run([arduino_cli(), 'core', 'install', CORE])
        elif args.action == 'setup-host':
            if zig_compiler():
                print('主機偵錯編譯器已可用。', flush=True)
                run([zig_compiler(), 'version'])
            else:
                run([sys.executable, '-m', 'pip', 'install', '--disable-pip-version-check',
                     '--target', project_path('.tools/host-compiler'), 'ziglang==0.16.0'])
        elif args.action == 'ports':
            run([arduino_cli(), 'board', 'list', '--no-color'])
        elif args.action in {'build', 'rebuild'}:
            compile_firmware(clean=args.action == 'rebuild')
        elif args.action in {'upload', 'upload-monitor'}:
            upload(port)
            if args.action == 'upload-monitor':
                monitor(port)
        elif args.action == 'monitor':
            monitor(port)
        elif args.action == 'test':
            run([sys.executable, '-m', 'unittest', 'discover', '-s', ROOT / 'mega2560/test', '-p', 'test_dev.py'])
            host_tests()
        elif args.action == 'native-debug':
            host_tests(debug=True, suite=args.suite)
        elif args.action == 'diagrams':
            run([sys.executable, ROOT / 'mega2560/generate_wiring.py'])
        return 0
    except (FileNotFoundError, ValueError) as error:
        print(f'MEGA：{error}', file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        print(f'MEGA：操作失敗（代碼 {error.returncode}），後續步驟已停止。', file=sys.stderr)
        if args.action in {'upload', 'monitor', 'upload-monitor'}:
            print('若連接埠被占用，先在原串列監控按 Ctrl+C；也確認 COM 埠及 USB 資料線。', file=sys.stderr)
        return error.returncode if error.returncode > 0 else 1
    except KeyboardInterrupt:
        print('\nMEGA：操作已結束。', flush=True)
        return 130


if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8')
    _process_job = configure_process_lifetime()
    raise SystemExit(main())
