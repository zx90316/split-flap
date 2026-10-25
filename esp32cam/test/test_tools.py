# SPDX-License-Identifier: GPL-3.0-only
import contextlib
import gzip
import hashlib
import http.client
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import threading
import unittest
import urllib.error
import urllib.parse
import urllib.request
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


dev = module('cam_dev_test', 'esp32cam/tools/dev.py')
mock = module('mock_web_test', 'esp32cam/test/mock_web.py')
PORT = 'COM4' if os.name == 'nt' else '/dev/ttyUSB0'


class WorkflowTest(unittest.TestCase):
    def invoke(self, args):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return dev.main(args)

    @patch.object(dev.shared, 'arduino_cli', return_value='arduino-cli')
    @patch.object(dev, 'package')
    @patch.object(dev, 'run')
    def test_upload_rebuilds_then_verifies_before_monitor(self, run, package, _cli):
        self.assertEqual(self.invoke(['upload-monitor', '--port', PORT]), 0)
        commands = [call.args[0] for call in run.call_args_list]
        self.assertEqual([str(c[1]) for c in commands[1:]], ['compile', 'upload', 'monitor'])
        self.assertIn('--verify', commands[2])
        self.assertIn('upload.speed=115200', commands[2])
        self.assertIn('dtr=off', commands[3])
        self.assertIn('rts=off', commands[3])
        self.assertIn('PartitionScheme=min_spiffs', dev.FQBN)
        package.assert_called_once()

    @patch.object(dev.shared, 'arduino_cli', return_value='arduino-cli')
    @patch.object(dev, 'package')
    @patch.object(dev, 'run', side_effect=[None, subprocess.CalledProcessError(2, ['compile'])])
    def test_bad_build_never_packages_uploads_or_monitors(self, run, package, _cli):
        self.assertEqual(self.invoke(['upload-monitor', '--port', PORT]), 2)
        self.assertEqual(run.call_count, 2)
        package.assert_not_called()

    @patch.object(dev.shared, 'arduino_cli', return_value='arduino-cli')
    @patch.object(dev, 'package')
    @patch.object(dev, 'run', side_effect=[None, None, subprocess.CalledProcessError(3, ['upload'])])
    def test_bad_upload_never_monitors(self, run, _package, _cli):
        self.assertEqual(self.invoke(['upload-monitor', '--port', PORT]), 3)
        self.assertEqual(run.call_count, 3)

    @patch.object(dev, 'run')
    def test_ports_are_required_and_checked(self, run):
        self.assertEqual(self.invoke(['upload']), 1)
        self.assertEqual(self.invoke(['monitor', '--port', 'COM4;ARM']), 1)
        run.assert_not_called()

    @patch.object(dev.shared, 'arduino_cli', return_value='arduino-cli')
    @patch.object(dev, 'package')
    @patch.object(dev, 'run')
    def test_build_never_opens_serial(self, run, _package, _cli):
        self.assertEqual(self.invoke(['build']), 0)
        for call in run.call_args_list:
            self.assertNotIn('--port', call.args[0])
            self.assertNotIn('upload', call.args[0])


class ArtifactsTest(unittest.TestCase):
    def test_ota_package_chip_hash_length_and_roundtrip(self):
        scratch = ROOT / 'esp32cam/test/.build'
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            path = Path(directory)
            image = bytearray(1024)
            image[0] = 0xe9
            binary = path / 'controller.ino.bin'
            binary.write_bytes(image)
            with contextlib.redirect_stdout(io.StringIO()):
                package = dev.package(binary, path / 'dist')
            data = package.read_bytes()
            self.assertEqual(len(data), 64 + len(image))
            self.assertEqual(data[16:48], hashlib.sha256(image).digest())
            self.assertEqual(mock.verify_package(data), image)
            for broken in [data[:-1], data + b'x', b'BAD' + data[3:], data[:80] + bytes([data[80] ^ 1]) + data[81:]]:
                with self.assertRaises(ValueError):
                    mock.verify_package(broken)
            image[12] = 1
            binary.write_bytes(image)
            with self.assertRaises(ValueError):
                dev.package(binary, path / 'dist')

    def test_ota_rejects_wrong_protocol_project_and_too_large(self):
        image = bytearray(256)
        image[0] = 0xe9
        data = b'SFCAM01\0' + struct.pack('<II', len(image), 2) + hashlib.sha256(image).digest() + b'splitflap-cam-v1' + image
        with self.assertRaises(ValueError):
            mock.verify_package(data)
        with self.assertRaises(ValueError):
            mock.verify_package(data[:48] + b'another-device-x' + data[64:])

    def test_embedded_page_is_exact_current_source(self):
        header = (ROOT / 'esp32cam/controller/web_assets.h').read_text(encoding='utf-8')
        embed = module('embed_assets_test', 'esp32cam/tools/embed_web.py')
        for name, path in embed.ASSETS.items():
            array = re.search(r'\b' + name + r'\[\].*?\{(.*?)\};', header, re.S)
            self.assertIsNotNone(array, name)
            compressed = bytes(int(n, 16) for n in re.findall(r'0x([0-9a-f]{2})', array.group(1)))
            self.assertEqual(gzip.decompress(compressed), path.read_bytes(), name)

    def test_vscode_references_and_both_wiring_files(self):
        tasks = json.loads((ROOT / '.vscode/tasks.json').read_text(encoding='utf-8'))
        launches = json.loads((ROOT / '.vscode/launch.json').read_text(encoding='utf-8'))
        labels = [t['label'] for t in tasks['tasks']]
        self.assertEqual(len(labels), len(set(labels)))
        defaults = [t for t in tasks['tasks'] if isinstance(t.get('group'), dict) and t['group'].get('isDefault')]
        self.assertEqual(len(defaults), 1)
        for config in launches['configurations']:
            if 'preLaunchTask' in config:
                self.assertIn(config['preLaunchTask'], labels)
        for name in ['wiring-runtime.svg', 'wiring-flashing.svg']:
            from xml.etree import ElementTree
            ElementTree.parse(ROOT / 'esp32cam' / name)


class FixtureClient:
    """One local HTTP connection; consume error bodies before the next request."""
    def __init__(self, url):
        parsed = urllib.parse.urlparse(url)
        self.connection = http.client.HTTPConnection(parsed.hostname, parsed.port, timeout=3)
        self.cookie = ''

    def close(self):
        self.connection.close()

    def open(self, request, data=None, timeout=3):
        if isinstance(request, urllib.request.Request):
            url, method, data = request.full_url, request.get_method(), request.data
            headers = dict(request.header_items())
        else:
            url, method, headers = request, 'POST' if data is not None else 'GET', {}
        path = urllib.parse.urlsplit(url).path or '/'
        if self.cookie:
            headers['Cookie'] = self.cookie
        if data is not None:
            headers.setdefault('Content-Type', 'application/x-www-form-urlencoded')
        self.connection.request(method, path, body=data, headers=headers)
        response = self.connection.getresponse()
        contents = response.read()
        if response.getheader('Set-Cookie'):
            self.cookie = response.getheader('Set-Cookie').split(';')[0]
        if response.status == 303:
            return self.open(urllib.parse.urljoin(url, response.getheader('Location')))
        if response.status >= 400:
            raise urllib.error.HTTPError(url, response.status, response.reason, response.headers, io.BytesIO(contents))
        return io.BytesIO(contents)


class HttpFixtureTest(unittest.TestCase):
    def setUp(self):
        self.server, self.model = mock.create_server(0)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = f'http://127.0.0.1:{self.server.server_port}'
        self.clients = []

    def tearDown(self):
        for client in self.clients:
            client.close()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=3)

    def client(self):
        client = FixtureClient(self.url)
        self.clients.append(client)
        return client

    def login(self, client):
        data = urllib.parse.urlencode({'username': 'admin', 'password': self.model.admin_password}).encode()
        client.open(self.url + '/mock/login', data=data, timeout=3).read()

    def post(self, client, path, body, csrf=True):
        request = urllib.request.Request(self.url + path, data=json.dumps(body).encode(), headers={'Content-Type': 'application/json'})
        if csrf:
            request.add_header('X-CSRF-Token', self.model.csrf)
        return json.load(client.open(request, timeout=3))

    def test_fixture_auth_and_csrf_gate(self):
        client = self.client()
        with self.assertRaises(urllib.error.HTTPError) as error:
            client.open(self.url + '/api/state', timeout=3)
        self.assertEqual(error.exception.code, 401)
        self.login(client)
        with self.assertRaises(urllib.error.HTTPError) as error:
            self.post(client, '/api/command', {'op': 'STOP'}, csrf=False)
        self.assertEqual(error.exception.code, 403)

    def test_fixture_wifi_without_mega_preserves_motion_and_maintenance_guards(self):
        client = self.client()
        self.login(client)
        self.post(client, '/mock/control', {'connected': False})
        state = json.load(client.open(self.url + '/api/state', timeout=3))
        self.assertTrue(state['networkEditable'])
        self.post(client, '/api/network', {'ssid': 'First-Setup-WiFi', 'password': 'TestNetwork123'})
        self.assertEqual(self.model.network['ssid'], 'First-Setup-WiFi')
        with self.assertRaises(urllib.error.HTTPError):
            self.post(client, '/api/command', dict(boot=state['boot'], request=state['nextRequest'], op='ARM', unit='ALL'))
        self.assertFalse(any(u['armed'] for u in self.model.units))
        self.post(client, '/mock/control', {'maintenance': True})
        self.assertFalse(json.load(client.open(self.url + '/api/state', timeout=3))['networkEditable'])
        with self.assertRaises(urllib.error.HTTPError):
            self.post(client, '/api/network', {'ssid': 'Blocked-Network'})
        self.assertEqual(self.model.network['ssid'], 'First-Setup-WiFi')
        self.post(client, '/mock/control', {'connected': True, 'maintenance': False})

    def test_fixture_manual_glyphs_sparse_layout_and_atomic_validation(self):
        client = self.client()
        self.login(client)
        state = json.load(client.open(self.url + '/api/state', timeout=3))
        result = self.post(client, '/api/command', {'boot': state['boot'], 'request': state['nextRequest'], 'op': 'SETTEXT', 'unit': 'ALL', 'value': 'Ä'})
        duplicate = self.post(client, '/api/command', {'boot': state['boot'], 'request': state['nextRequest'], 'op': 'SETTEXT', 'unit': 'ALL', 'value': 'Ä'})
        self.assertEqual(result, duplicate)
        self.assertEqual(self.model.units[0]['index'], 27)
        config = copy = json.loads(json.dumps(self.model.schema))
        config['units'] = [7, 0, 2]
        response = self.post(client, '/api/config', dict(schema=1, boot=self.model.boot, request=self.model.next_request, mega=config))
        self.assertIn('id', response)
        self.assertEqual(self.model.schema['units'], [7, 0, 2])
        self.assertFalse(self.model.units[1]['enabled'])
        bad = json.loads(json.dumps(copy))
        bad['units'] = [0, 0]
        with self.assertRaises(urllib.error.HTTPError):
            self.post(client, '/api/config', dict(schema=1, boot=self.model.boot, request=self.model.next_request, mega=bad))
        self.assertEqual(self.model.schema['units'], [7, 0, 2])

    def test_fixture_ota_requires_stop_ticket_and_clears_failed_attempt(self):
        client = self.client()
        self.login(client)
        boot = self.model.boot
        boundary = 'SplitFlapTestBoundary'
        data = (f'--{boundary}\r\nContent-Disposition: form-data; name="firmware"; filename="bad.sffw"\r\n'
                'Content-Type: application/octet-stream\r\n\r\nBAD\r\n'
                f'--{boundary}--\r\n').encode()
        def upload(ticket):
            request = urllib.request.Request(self.url + '/api/update', data=data, headers={
                'Content-Type': f'multipart/form-data; boundary={boundary}',
                'X-CSRF-Token': self.model.csrf, 'X-OTA-Ticket': ticket})
            return client.open(request, timeout=3)
        with self.assertRaises(urllib.error.HTTPError):
            upload('unprepared')
        self.assertEqual(self.model.boot, boot)
        state = json.load(client.open(self.url + '/api/state', timeout=3))
        prepared = self.post(client, '/api/ota/prepare', dict(boot=boot, request=state['nextRequest']))
        self.assertTrue(self.model.ota_ready)
        with self.assertRaises(urllib.error.HTTPError):
            upload(prepared['ticket'])
        self.assertFalse(self.model.ota_ready)
        self.assertEqual(self.model.ticket, '')
        self.assertEqual(self.model.boot, boot)


if __name__ == '__main__':
    unittest.main()
