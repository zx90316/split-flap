#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise upload ordering and process cleanup without touching a serial port."""
import contextlib
import ctypes
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('mega_dev', Path(__file__).resolve().parents[1] / 'tools/dev.py')
dev = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(dev)
PORT = 'COM3' if os.name == 'nt' else '/dev/ttyACM0'


class WorkflowTest(unittest.TestCase):
    def invoke(self, args):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return dev.main(args)

    @patch.object(dev, 'arduino_cli', return_value='arduino-cli')
    @patch.object(dev, 'run')
    def test_compile_before_verified_upload(self, run, _cli):
        self.assertEqual(self.invoke(['upload', '--port', PORT]), 0)
        calls = [call.args[0] for call in run.call_args_list]
        self.assertEqual([args[1] for args in calls], ['compile', 'upload'])
        self.assertIn('--verify', calls[1])
        self.assertEqual(calls[1][calls[1].index('--port') + 1], PORT)
        self.assertIn(dev.FQBN, calls[1])

    @patch.object(dev, 'arduino_cli', return_value='arduino-cli')
    @patch.object(dev, 'run', side_effect=subprocess.CalledProcessError(5, ['compile']))
    def test_compile_failure_never_uploads_old_firmware(self, run, _cli):
        self.assertEqual(self.invoke(['upload-monitor', '--port', PORT]), 5)
        self.assertEqual(run.call_count, 1)
        self.assertEqual(run.call_args.args[0][1], 'compile')

    @patch.object(dev, 'arduino_cli', return_value='arduino-cli')
    @patch.object(dev, 'run', side_effect=[None, subprocess.CalledProcessError(2, ['upload'])])
    def test_failed_upload_never_opens_monitor(self, run, _cli):
        self.assertEqual(self.invoke(['upload-monitor', '--port', PORT]), 2)
        self.assertEqual(run.call_count, 2)

    @patch.object(dev, 'arduino_cli', return_value='arduino-cli')
    @patch.object(dev, 'run')
    def test_monitor_only_after_successful_upload(self, run, _cli):
        self.assertEqual(self.invoke(['upload-monitor', '--port', PORT]), 0)
        self.assertEqual([call.args[0][1] for call in run.call_args_list], ['compile', 'upload', 'monitor'])
        self.assertIn('baudrate=115200', run.call_args.args[0])

    @patch.object(dev, 'run')
    def test_missing_port_does_not_run_tools(self, run):
        self.assertEqual(self.invoke(['upload']), 1)
        run.assert_not_called()

    @unittest.skipUnless(os.name == 'nt', 'Windows COM validation')
    @patch.object(dev, 'run')
    def test_malformed_port_does_not_run_tools(self, run):
        self.assertEqual(self.invoke(['upload', '--port', 'COM3;something']), 1)
        self.assertEqual(self.invoke(['monitor', '--port', 'COM0']), 1)
        run.assert_not_called()

    @patch.object(dev, 'arduino_cli', return_value='arduino-cli')
    @patch.object(dev, 'run')
    def test_build_never_opens_a_port(self, run, _cli):
        self.assertEqual(self.invoke(['build']), 0)
        self.assertEqual(run.call_count, 1)
        self.assertEqual(run.call_args.args[0][1], 'compile')
        self.assertNotIn('--port', run.call_args.args[0])
        self.assertNotIn('--upload', run.call_args.args[0])

    def test_paths_and_environment_stay_in_workspace(self):
        with self.assertRaises(ValueError):
            dev.project_path('../outside')
        env = dev.environment()
        for key in ['ARDUINO_DIRECTORIES_DATA', 'ARDUINO_DIRECTORIES_DOWNLOADS', 'ARDUINO_DIRECTORIES_USER']:
            self.assertTrue(Path(env[key]).is_relative_to(dev.ROOT))
        self.assertEqual(env['PYTHONUTF8'], '1')

    @unittest.skipUnless(os.name == 'nt', 'Windows debugger process lifetime')
    def test_stopping_runner_closes_descendants(self):
        build = dev.ROOT / 'mega2560/test/.build'
        build.mkdir(exist_ok=True)
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        kernel.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        with tempfile.TemporaryDirectory(dir=build) as scratch:
            pid_file = Path(scratch) / 'child.pid'
            code = (
                'import importlib.util,subprocess,sys,time;from pathlib import Path;'
                f's=importlib.util.spec_from_file_location("dev",{str(SPEC.origin)!r});'
                'd=importlib.util.module_from_spec(s);s.loader.exec_module(d);'
                'job=d.configure_process_lifetime();'
                'child=subprocess.Popen([sys.executable,"-c","import time;time.sleep(60)"]);'
                f'Path({str(pid_file)!r}).write_text(str(child.pid));'
                'time.sleep(60)'
            )
            parent = subprocess.Popen([sys.executable, '-c', code], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            handle = None
            try:
                deadline = time.monotonic() + 5
                while not pid_file.exists() and time.monotonic() < deadline and parent.poll() is None:
                    time.sleep(0.02)
                self.assertTrue(pid_file.exists(), 'Process-lifetime helper did not start')
                handle = kernel.OpenProcess(0x100001, False, int(pid_file.read_text()))
                self.assertTrue(handle)
                parent.terminate()
                parent.wait(timeout=3)
                self.assertEqual(kernel.WaitForSingleObject(handle, 3000), 0, 'Child tool remained running')
            finally:
                if parent.poll() is None:
                    parent.terminate()
                parent.communicate(timeout=3)
                if handle:
                    if kernel.WaitForSingleObject(handle, 0) != 0:
                        kernel.TerminateProcess(handle, 1)
                        kernel.WaitForSingleObject(handle, 3000)
                    kernel.CloseHandle(handle)


if __name__ == '__main__':
    unittest.main()
