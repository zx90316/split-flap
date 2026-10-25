#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Browser-only HTTP fixture. No serial ports. Firmware core is tested by test_bridge.cpp."""
import argparse
import copy
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import secrets
import struct
import threading
import time
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[2]
GLYPHS = [' ', *'ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'Ä', 'Ö', 'Ü', *'0123456789', ':', '.', '-', '?', '!']


def verify_package(data):
    if len(data) < 320 or data[:8] != b'SFCAM01\0' or data[48:64] != b'splitflap-cam-v1':
        raise ValueError('只接受本專案的 .sffw 檔案。')
    size, protocol = struct.unpack('<II', data[8:16])
    if protocol != 1 or size > 1966080 or size != len(data) - 64:
        raise ValueError('映像長度或協定版本不符。')
    image = data[64:]
    if image[0] != 0xe9 or image[12:14] != b'\0\0' or hashlib.sha256(image).digest() != data[16:48]:
        raise ValueError('映像晶片或 SHA-256 不符。')
    return image


class Model:
    def __init__(self):
        self.lock = threading.RLock()
        self.schema = {'units': [0], 'calibration': [dict(revolution=4096, offset=0, speed=5000, accel=400,
                       direction=1, order='1234', activeLow=True) for _ in range(10)]}
        self.display = dict(mode='text', clockFormat='HH:MM', dateFormat='YYYY-MM-DD', timezone=480,
                            align=0, running=False, reason='請先定位並啟用。', timeValid=False, timeSource='none', unix=0)
        self.units = [dict(unit=i, enabled=i == 0, armed=False, index=0, target=0, mode=0, reference='NONE',
                          rawHigh=True, hall=False, steps=0, remaining=0, measured=[], error='') for i in range(10)]
        self.boot = 1234567
        self.next_request = 1
        self.jobs = []
        self.pending = None
        self.connected = True
        self.maintenance = False
        self.ticket = ''
        self.ota_ready = False
        self.network = dict(ssid='', apSsid='SplitFlap-MOCK', apIp='192.168.4.1', staIp='0.0.0.0', connected=False, rssi=-45)
        self.admin_password = 'MockPassword123'
        self.session = secrets.token_hex(12)
        self.csrf = 'mock-csrf-token'

    def advance(self):
        if self.pending and time.monotonic() >= self.pending[0]:
            _, callback, job = self.pending
            self.pending = None
            callback()
            job['stage'] = 'done'
        self.display['unix'] = int(time.time()) if self.display['timeValid'] else 0

    def stop(self):
        if self.pending:
            self.pending[2].update(stage='fault', error='stopped')
        self.pending = None
        for unit in self.units:
            unit.update(mode=0, armed=False, reference='NONE', remaining=0)
        self.display.update(running=False, reason='已停止，請重新定位。')

    def state(self):
        self.advance()
        return dict(firmware='1.0.1-mock', protocol=1, boot=self.boot, nextRequest=self.next_request, csrf=self.csrf,
                    connected=self.connected, protocolMismatch=False, uncertain=False, maintenance=self.maintenance, storageOk=True,
                    networkEditable=not self.pending and not self.maintenance,
                    ageMs=0, digits=len(self.schema['units']), selected=0, busy=bool(self.pending), saving=False,
                    watching=False, usbLocked=False, error='', mega=self.schema, units=self.units, jobs=self.jobs[-16:],
                    display=self.display, network=self.network, otaReady=self.ota_ready, otaError='')

    def new_job(self, body, op, stop=False):
        if body.get('boot') != self.boot:
            raise ValueError('設備已重新啟動。')
        request = body.get('request')
        if request < self.next_request:
            for job in self.jobs:
                if job['request'] == request:
                    return job, True
            raise ValueError('unknown')
        if request != self.next_request:
            raise ValueError('invalid')
        if not self.connected:
            raise ValueError('link_lost')
        if self.pending and not stop:
            raise ValueError('busy')
        self.next_request += 1
        job = dict(id=self.next_request, request=request, op=op, stage='accepted', error='none')
        self.jobs.append(job)
        return job, False

    def text_indices(self, value, exact=False, align=0):
        import unicodedata
        text = unicodedata.normalize('NFC', value).upper().replace('_', ' ').replace('$', 'Ä').replace('&', 'Ö').replace('#', 'Ü')
        width = len(self.schema['units'])
        if len(text) > width or exact and len(text) != width or any(c not in GLYPHS for c in text):
            raise ValueError('字元或長度無效。')
        padding = width - len(text)
        left = padding if align == 2 else padding // 2 if align == 1 else 0
        return [0] * left + [GLYPHS.index(c) for c in text] + [0] * (padding - left)

    def command(self, body):
        op = body['op']
        job, repeated = self.new_job(body, op, stop=op in {'STOP', 'OFF', 'CLAIM'})
        if repeated:
            return {'id': job['id']}
        self.display.update(running=False, reason='已切換為手動操作。')
        if op in {'STOP', 'OFF', 'CLAIM'}:
            self.stop()
            job['stage'] = 'done'
            return {'id': job['id']}
        targets = self.schema['units'] if body.get('unit', 'ALL') == 'ALL' else [int(body['unit'])]
        if any(u not in self.schema['units'] for u in targets):
            job.update(stage='rejected', error='invalid')
            return {'id': job['id']}
        values = None
        if op in {'SETTEXT', 'TEXT'}:
            values = self.text_indices(body.get('value', ''), op == 'SETTEXT', body.get('align', 0))
        elif op in {'SET', 'SETINDEX', 'SHOW', 'GOTO'}:
            value = body['value']
            values = [int(value) if isinstance(value, int) else GLYPHS.index(value.upper())]
        if op == 'ARM':
            for u in targets:
                self.units[u]['armed'] = True
            job['stage'] = 'done'
        elif op in {'SETTEXT', 'SET', 'SETINDEX'}:
            for u, value in zip(targets, values):
                self.units[u].update(index=value, target=value, reference='MANUAL', armed=False)
            job['stage'] = 'done'
        elif op in {'SAVE', 'LOAD', 'UNIT', 'WATCH'}:
            job['stage'] = 'done'
        elif op in {'SPEED', 'ACCEL'}:
            for u in targets:
                self.schema['calibration'][u]['speed' if op == 'SPEED' else 'accel'] = body['value']
                self.units[u].update(reference='NONE', armed=False)
            job['stage'] = 'done'
        else:
            needs_position = op in {'TEXT', 'SHOW', 'GOTO', 'FLAP', 'CYCLE'}
            if any(not self.units[u]['armed'] or needs_position and self.units[u]['reference'] == 'NONE' for u in targets):
                job.update(stage='rejected', error='not_ready')
            else:
                if values is None:
                    values = [0 if op == 'HOME' else (self.units[u]['index'] + 1) % 45 for u in targets]
                for u, value in zip(targets, values):
                    self.units[u].update(target=value, mode=7, remaining=91)

                def complete():
                    for u, value in zip(targets, values):
                        self.units[u].update(index=value, target=value, mode=0, remaining=0, steps=91)
                        if op == 'HOME':
                            self.units[u]['reference'] = 'HALL'
                self.pending = (time.monotonic() + 0.7, complete, job)
        return {'id': job['id']}


def handler_for(model):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def log_message(self, *args):
            pass

        def send_json(self, data, code=200):
            value = json.dumps(data, ensure_ascii=False).encode()
            self.send_response(code)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(value)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(value)

        def logged_in(self):
            return f'MockSession={model.session}' in self.headers.get('Cookie', '')

        def do_GET(self):
            path = urlparse(self.path).path
            if path == '/mock/login':
                data = '<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>模擬器登入</title><h1>翻頁控制台 · 測試登入</h1><p>此為瀏覽器測試用登入頁，正式韌體使用瀏覽器 Digest 登入。</p><form method="post"><label>帳號<input name="username" autocomplete="username"></label><label>密碼<input name="password" type="password" autocomplete="current-password"></label><button>登入</button></form></html>'.encode()
                self.send_response(200)
                self.send_header('Content-Type', 'text/html; charset=utf-8')
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return
            if not self.logged_in():
                if path.startswith('/api/'):
                    self.send_json({'error': 'login_required'}, 401)
                else:
                    self.send_response(303)
                    self.send_header('Location', '/mock/login')
                    self.send_header('Content-Length', '0')
                    self.end_headers()
                return
            with model.lock:
                if path == '/api/state':
                    self.send_json(model.state())
                elif path == '/api/config':
                    self.send_json(dict(schema=1, device='splitflap-esp32cam', mega=model.schema,
                                        display={k: model.display[k] for k in ['mode', 'clockFormat', 'dateFormat', 'timezone', 'align']}))
                elif path == '/':
                    page = (ROOT / 'esp32cam/web/index.html').read_bytes()
                    self.send_response(200)
                    self.send_header('Content-Type', 'text/html; charset=utf-8')
                    self.send_header('Content-Length', str(len(page)))
                    self.end_headers()
                    self.wfile.write(page)
                else:
                    self.send_json({'error': 'not_found'}, 404)

        def do_POST(self):
            path = urlparse(self.path).path
            data = self.rfile.read(min(int(self.headers.get('Content-Length', 0)), 2100000))
            if path == '/mock/login':
                values = parse_qs(data.decode())
                if values.get('username') == ['admin'] and values.get('password') == [model.admin_password]:
                    self.send_response(303)
                    self.send_header('Set-Cookie', f'MockSession={model.session}; HttpOnly; SameSite=Strict; Path=/')
                    self.send_header('Location', '/')
                    self.send_header('Content-Length', '0')
                    self.end_headers()
                else:
                    self.send_json({'error': '登入失敗'}, 401)
                return
            if not self.logged_in():
                self.send_json({'error': 'login_required'}, 401)
                return
            if self.headers.get('X-CSRF-Token') != model.csrf:
                self.send_json({'error': 'csrf'}, 403)
                return
            with model.lock:
                try:
                    if path == '/api/update':
                        if self.headers.get('X-OTA-Ticket') != model.ticket or not model.ota_ready:
                            raise ValueError('尚未準備更新。')
                        boundary = self.headers['Content-Type'].split('boundary=')[1].strip('"').encode()
                        part = data.split(b'--' + boundary)[1]
                        image = part.split(b'\r\n\r\n', 1)[1][:-2]
                        verify_package(image)
                        model.stop()
                        model.boot += 1
                        model.next_request = 1
                        model.jobs = []
                        model.ticket = ''
                        model.ota_ready = False
                        self.send_json({'message': '更新完成，模擬設備已重新啟動。'})
                        return
                    body = json.loads(data or b'{}')
                    model.advance()
                    if path == '/api/command':
                        self.send_json(model.command(body), 202)
                    elif path == '/api/config':
                        settings = copy.deepcopy(body['mega'])
                        units = settings['units']
                        if body.get('schema') != 1 or not 1 <= len(units) <= 10 or len(set(units)) != len(units) or any(type(u) is not int or not 0 <= u <= 9 for u in units) or len(settings['calibration']) != 10:
                            raise ValueError('整份設定驗證失敗。')
                        for c in settings['calibration']:
                            if not 1000 <= c['revolution'] <= 20000 or not 0 <= c['offset'] < c['revolution'] or not 800 <= c['speed'] <= 30000 or not 100 <= c['accel'] <= 6000 or sorted(c['order']) != list('1234'):
                                raise ValueError('校準參數無效。')
                        job, repeated = model.new_job(body, 'CONFIG')
                        if not repeated:
                            model.stop()
                            model.schema = settings
                            for u in model.units:
                                u['enabled'] = u['unit'] in units
                            if 'display' in body:
                                model.display.update(body['display'])
                            job['stage'] = 'done'
                        self.send_json({'id': job['id']}, 202)
                    elif path == '/api/time':
                        if not 1704067200 <= body['unix'] < 4102444800:
                            raise ValueError('時間無效。')
                        model.display.update(timeValid=True, timeSource='phone', unix=body['unix'])
                        self.send_json({})
                    elif path == '/api/preferences':
                        if body.get('run'):
                            if len(model.schema['units']) < 4 or not model.display['timeValid'] or any(not model.units[u]['armed'] or model.units[u]['reference'] == 'NONE' for u in model.schema['units']):
                                raise ValueError('請先定位、啟用並校時。')
                        model.display.update(body)
                        model.display['running'] = body.get('run', False)
                        model.display['reason'] = '自動顯示執行中。' if body.get('run') else '自動顯示已暫停。'
                        self.send_json({})
                    elif path == '/api/network':
                        if model.pending or model.maintenance:
                            raise ValueError('busy')
                        model.display['running'] = False
                        model.network.update(ssid=body['ssid'], connected=bool(body['ssid']), staIp='192.168.1.50')
                        self.send_json({'message': '模擬網路設定已保存。'})
                    elif path == '/api/ota/prepare':
                        model.stop()
                        job, _ = model.new_job(body, 'STOP', stop=True)
                        job['stage'] = 'done'
                        model.ticket = secrets.token_hex(16)
                        model.ota_ready = True
                        self.send_json({'id': job['id'], 'ticket': model.ticket}, 202)
                    elif path == '/mock/control':
                        if 'maintenance' in body:
                            model.maintenance = bool(body['maintenance'])
                        if 'connected' in body:
                            model.connected = body['connected']
                            if not model.connected:
                                model.stop()
                        if body.get('reboot'):
                            model.stop()
                            model.boot += 1
                            model.next_request = 1
                            model.jobs = []
                        self.send_json({})
                    else:
                        self.send_json({'error': 'not_found'}, 404)
                except (ValueError, KeyError, TypeError) as error:
                    if path == '/api/update':
                        model.ticket = ''
                        model.ota_ready = False
                    self.send_json({'error': str(error)}, 409)
    return Handler


def create_server(port=8765):
    model = Model()
    class MockServer(ThreadingHTTPServer):
        request_queue_size = 64
    return MockServer(('127.0.0.1', port), handler_for(model)), model


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    server, _model = create_server(args.port)
    print(f'Mock web http://127.0.0.1:{server.server_port} — admin / MockPassword123 (no hardware)', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
