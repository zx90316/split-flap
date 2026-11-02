#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Paced, authenticated LAN OTA. Never ARM/HOME or retry a submitted upload.

Set SF_DEVICE_ADMIN_PASSWORD in the environment. Close old controller tabs first.
The existing firmware's STOP/fresh-state/maintenance checks remain authoritative.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import http.client
import http.cookiejar
import json
import os
from pathlib import Path
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[2]


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class DeviceClient:
    def __init__(self, base, password):
        url = urllib.parse.urlsplit(base)
        if url.scheme != 'http' or not url.hostname or url.username or url.password or url.path not in ('', '/') or url.query or url.fragment:
            raise ValueError('Use the controller HTTP origin without credentials or a path.')
        self.base = base.rstrip('/')
        self.url = url
        self.password = password
        manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        manager.add_password(None, self.base, 'admin', password)
        self.digest = urllib.request.HTTPDigestAuthHandler(manager)
        self.challenge = None
        self.cookies = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}),
                                                  urllib.request.HTTPCookieProcessor(self.cookies), NoRedirect())

    def authorization(self, request):
        if self.challenge:
            value = self.digest.get_authorization(request, self.challenge)
            if not value:
                raise RuntimeError('Cannot construct Digest authorization.')
            request.add_unredirected_header('Authorization', 'Digest ' + value)
        self.cookies.add_cookie_header(request)

    def request(self, path, body=None, headers=None, authenticate=True):
        data = None if body is None else json.dumps(body).encode()
        login_attempted = False
        for attempt in range(3):
            request = urllib.request.Request(self.base + path, data=data,
                                             headers={'Content-Type': 'application/json', **(headers or {})})
            self.authorization(request)
            try:
                with self.opener.open(request, timeout=12) as response:
                    raw = response.read()
                    return response.status, json.loads(raw)
            except urllib.error.HTTPError as error:
                raw = error.read()
                challenge = error.headers.get('WWW-Authenticate', '')
                if error.code == 401 and authenticate and attempt < 2:
                    if challenge.startswith('Digest '):
                        self.challenge = urllib.request.parse_keqv_list(urllib.request.parse_http_list(challenge[7:]))
                        continue
                    if not login_attempted:
                        login_attempted = True
                        status, result = self.request('/api/login', {'username': 'admin', 'password': self.password}, authenticate=False)
                        if status != 200:
                            raise RuntimeError('Controller login was rejected.')
                        self.challenge = None
                        continue
                try:
                    result = json.loads(raw)
                except (ValueError, UnicodeDecodeError):
                    result = {'error': 'Non-JSON error response'}
                return error.code, result
        raise RuntimeError('Authentication did not complete.')

    def get(self, path):
        status, result = self.request(path)
        if status != 200:
            raise RuntimeError(f'GET {path}: HTTP {status}: {result.get("error", "")}')
        return result

    def upload(self, package, csrf, ticket, chunk_size=2048, interval=0.06):
        boundary = 'SplitFlapPaced-' + os.urandom(12).hex()
        prefix = (f'--{boundary}\r\nContent-Disposition: form-data; name="firmware"; filename="splitflap-esp32s.sffw"\r\n'
                  'Content-Type: application/octet-stream\r\n\r\n').encode()
        suffix = f'\r\n--{boundary}--\r\n'.encode()
        data = prefix + package + suffix
        request = urllib.request.Request(self.base + '/api/update', data=b'', method='POST')
        self.authorization(request)
        # New firmware waits for a fresh Mega snapshot after physical Flash writes.
        connection = http.client.HTTPConnection(self.url.hostname, self.url.port or 80, timeout=300)
        try:
            connection.putrequest('POST', '/api/update')
            for key, value in request.header_items():
                connection.putheader(key, value)
            connection.putheader('Content-Type', 'multipart/form-data; boundary=' + boundary)
            connection.putheader('Content-Length', str(len(data)))
            connection.putheader('X-CSRF-Token', csrf)
            connection.putheader('X-OTA-Ticket', ticket)
            connection.putheader('Connection', 'close')
            connection.endheaders()
            progress = -1
            for offset in range(0, len(data), chunk_size):
                connection.send(data[offset:offset + chunk_size])
                percent = min(100, (offset + chunk_size) * 100 // len(data))
                if percent // 10 != progress:
                    progress = percent // 10
                    print(f'Upload {percent}% (paced)', flush=True)
                time.sleep(interval)
            response = connection.getresponse()
            raw = response.read()
            try:
                result = json.loads(raw)
            except (ValueError, UnicodeDecodeError):
                result = {'error': 'Non-JSON upload response'}
            return response.status, result
        finally:
            connection.close()


def validate_package(path):
    data = path.read_bytes()
    if len(data) < 320 or data[:8] != b'SFESP01\0' or data[48:64] != b'splitflap-32s-v1':
        raise ValueError('Wrong OTA project/header.')
    length, protocol = struct.unpack('<II', data[8:16])
    if protocol != 1 or length > 1966080 or length != len(data) - 64:
        raise ValueError('Wrong OTA length/protocol.')
    image = data[64:]
    if image[0] != 0xe9 or image[12:14] != b'\0\0' or hashlib.sha256(image).digest() != data[16:48]:
        raise ValueError('Wrong ESP32 image or SHA-256.')
    return data


def canonical_mega(config):
    mega = config['mega']
    return {'units': mega.get('units', list(range(mega.get('digits', 1)))), 'calibration': mega['calibration']}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://splitflap.local')
    parser.add_argument('--file', type=Path, default=ROOT / 'esp32cam/dist/splitflap-esp32s.sffw')
    parser.add_argument('--version', required=True, help='Expected firmware version after restart')
    args = parser.parse_args(argv)
    password = os.environ.get('SF_DEVICE_ADMIN_PASSWORD')
    if not password:
        raise ValueError('Set SF_DEVICE_ADMIN_PASSWORD first.')
    package = validate_package(args.file)
    client = DeviceClient(args.url, password)
    before = client.get('/api/state')
    if before.get('hardware') != 'NodeMCU ESP-32S' or before.get('protocol') != 1:
        raise RuntimeError('This is not the expected controller.')
    if not before.get('connected') or any(before.get(k) for k in ('busy', 'saving', 'watching', 'maintenance', 'usbLocked', 'uncertain')):
        raise RuntimeError('Controller must be connected, idle and available before update.')
    if before.get('display', {}).get('running') or any(u.get('armed') for u in before.get('units', [])):
        raise RuntimeError('Pause and stop the controller before update.')
    config = client.get('/api/config')
    output = ROOT / '.tools' / ('ota-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
    output.mkdir(parents=True, exist_ok=False)
    (output / 'configuration-before.json').write_text(json.dumps(config, ensure_ascii=False, indent=2), encoding='utf-8')
    history = {'firmware': before['firmware'], 'units': [
        {'unit': u['unit'], 'rememberedIndex': u.get('rememberedIndex')} for u in before['units']]}
    (output / 'display-history-before.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
    status, prepared = client.request('/api/ota/prepare', {'boot': before['boot'], 'request': before['nextRequest']}, {'X-CSRF-Token': before['csrf']})
    if status != 202:
        raise RuntimeError(f'Preparation rejected: HTTP {status}: {prepared.get("error", "")}')
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        state = client.get('/api/state')
        job = next((j for j in state.get('jobs', []) if j['id'] == prepared['id']), None)
        if state['boot'] != before['boot'] or (job and job['stage'] in ('fault', 'rejected')):
            raise RuntimeError('STOP failed or device restarted before upload.')
        if state.get('otaReady') and job and job['stage'] == 'done':
            break
        time.sleep(0.25)
    else:
        raise RuntimeError('No fresh STOP confirmation; upload not sent.')
    print('STOP confirmed; sending the verified image with pauses between chunks.', flush=True)
    start = time.monotonic()
    status, result = client.upload(package, state['csrf'], prepared['ticket'])
    (output / 'upload-result.json').write_text(json.dumps({'status': status, 'result': result, 'seconds': time.monotonic() - start}, ensure_ascii=False, indent=2), encoding='utf-8')
    if status != 200:
        raise RuntimeError(f'Upload rejected (not retried): HTTP {status}: {result.get("error", "")}')
    print('Image accepted. Waiting for restart and checking settings.', flush=True)
    time.sleep(2)
    after = None
    deadline = time.monotonic() + 45
    # A new client negotiates either the old Digest or the new form login.
    client = DeviceClient(args.url, password)
    while time.monotonic() < deadline:
        try:
            state = client.get('/api/state')
            if state.get('firmware') == args.version and state.get('connected'):
                after = state
                break
        except (OSError, ValueError, RuntimeError):
            pass
        time.sleep(1)
    if after is None:
        raise RuntimeError('Image accepted, but the expected version/reconnection is not confirmed. Do not blindly re-upload.')
    updated = client.get('/api/config')
    if canonical_mega(config) != canonical_mega(updated):
        raise RuntimeError('Mega configuration differs after update; inspect backup before further operation.')
    for field in ('mode', 'clockFormat', 'dateFormat', 'timezone', 'align'):
        if config.get('display', {}).get(field) != updated.get('display', {}).get(field):
            raise RuntimeError('Display preferences changed unexpectedly: ' + field)
    if not after.get('storageOk') or not after.get('displayMemoryOk') or after.get('busy') or after.get('display', {}).get('running') or any(u.get('armed') for u in after['units']):
        raise RuntimeError('Post-update controller state needs inspection.')
    if [u.get('rememberedIndex') for u in before['units']] != [u.get('rememberedIndex') for u in after['units']]:
        raise RuntimeError('Last display history changed unexpectedly.')
    summary = {'beforeFirmware': before['firmware'], 'firmware': after['firmware'], 'connected': after['connected'],
               'configurationPreserved': True, 'displayHistoryPreserved': True, 'idleAndDisarmed': True,
               'storageOk': after['storageOk'], 'autoRecovery': after.get('display', {}).get('autoRecovery'),
               'packageBytes': len(package), 'sha256': hashlib.sha256(package[64:]).hexdigest()}
    (output / 'verification.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps(summary, ensure_ascii=False, indent=2), flush=True)
    print('Evidence directory: ' + str(output), flush=True)


if __name__ == '__main__':
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
    try:
        main()
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print('OTA: ' + str(error), file=sys.stderr)
        raise SystemExit(1)
