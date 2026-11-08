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
    if len(data) < 320 or data[:8] != b'SFESP01\0' or data[48:64] != b'splitflap-32s-v1':
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
        self.schema = {'units': [0], 'calibration': [dict(revolution=4096, offset=0, speed=800, accel=800,
                       direction=1, order='1234', activeLow=True) for _ in range(10)]}
        self.display = dict(mode='text', clockFormat='HH:MM', dateFormat='YYYY-MM-DD', timezone=480,
                            align=0, running=False, reason='請先歸零校準或記錄目前字元。', timeValid=False, timeSource='none', unix=0,
                            weekdayFormat='SHORT', leadingZero=True, limited=False, windowStart=0, windowEnd=0,
                            autoRecovery=False, itemIndex=0, items=[dict(mode='clock',format='HH:MM',text='',seconds=30,align=0)])
        self.units = [dict(unit=i, enabled=i == 0, armed=False, index=0, target=0, mode=0, reference='NONE',
                          rawHigh=True, hall=False, steps=0, remaining=0, measured=[], error='', rememberedIndex=None) for i in range(10)]
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
        self.sessions = set()
        self.workflow = dict(active=False,calibrating=False,calibrated=False,recovering=False,step=0,steps=0,message='待命',jogReadyMask=0)
        self.original = None
        self.recovery_chosen = False
        self.resume_display = False

    def advance(self):
        if self.pending and time.monotonic() >= self.pending[0]:
            _, callback, job = self.pending
            self.pending = None
            callback()
            job['stage'] = 'done'
            self.workflow['active'] = False
        self.display['unix'] = int(time.time()) if self.display['timeValid'] else 0

    def stop(self):
        if self.pending:
            self.pending[2].update(stage='fault', error='stopped')
        self.pending = None
        for unit in self.units:
            unit.update(mode=0, armed=False, reference='NONE', remaining=0)
        self.workflow['active'] = False
        self.display.update(running=False, reason='已停止，請重新定位。')

    def state(self):
        self.advance()
        result = dict(firmware='1.3.0-mock', hardware='NodeMCU ESP-32S', protocol=1, boot=self.boot, nextRequest=self.next_request, csrf=self.csrf,
                    displayMemoryOk=True, workflow=self.workflow,
                    connected=self.connected, protocolMismatch=False, uncertain=False, maintenance=self.maintenance, storageOk=True,
                    networkEditable=not self.pending and not self.maintenance and not self.workflow['calibrating'],
                    ageMs=0, digits=len(self.schema['units']), selected=0, busy=bool(self.pending), saving=False,
                    watching=False, usbLocked=False, error='', mega=self.schema, units=self.units, jobs=self.jobs[-16:],
                    display=self.display, network=self.network, otaReady=self.ota_ready, otaError='')
        result.update(self.single_state())
        return result

    single = False

    def single_state(self):
        if not self.single:
            return {}
        # Single controller: same API plus MCP23017 diagnostics; no Mega link.
        return dict(single=True, hardware='NodeMCU ESP-32S + MCP23017', firmware='2.0.0-mock',
                    mcp=dict(ready=self.connected, outputsOff=not self.pending, present=15 if self.connected else 14,
                             required=15, hall=True, configStorageOk=True, maxTickGapUs=310, lateTicks=0, maxFlushUs=120),
                    error='' if self.connected else 'mcp_missing')

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
        if op in {'ARM','UNITS','DIGITS'}:
            job.update(stage='rejected',error='invalid')
        elif op == 'ARM':
            for u in targets:
                self.units[u]['armed'] = True
            job['stage'] = 'done'
        elif op in {'SETTEXT', 'SET', 'SETINDEX'}:
            for u, value in zip(targets, values):
                self.units[u].update(index=value, target=value, reference='MANUAL', armed=False,rememberedIndex=value)
            job['stage'] = 'done'
        elif op in {'SAVE', 'LOAD', 'UNIT', 'WATCH'}:
            job['stage'] = 'done'
        elif op in {'SPEED', 'ACCEL'}:
            for u in targets:
                self.schema['calibration'][u]['speed' if op == 'SPEED' else 'accel'] = body['value']
            job['stage'] = 'done'
        else:
            needs_position = op in {'TEXT', 'SHOW', 'GOTO', 'FLAP', 'CYCLE'}
            for u in targets:
                self.units[u]['armed'] = True
                if self.workflow['calibrated'] and self.units[u]['reference'] == 'NONE':
                    self.units[u]['reference'] = 'HALL'
            if any(needs_position and self.units[u]['reference'] == 'NONE' for u in targets):
                job.update(stage='rejected', error='not_ready')
            else:
                if values is None:
                    values = [0 if op == 'HOME' else (self.units[u]['index'] + 1) % 45 for u in targets]
                for u, value in zip(targets, values):
                    self.units[u].update(target=value, mode=7, remaining=91)

                def complete():
                    for u, value in zip(targets, values):
                        self.units[u].update(index=value, target=value, mode=0, remaining=0, steps=91)
                        if op in {'TEXT','SHOW','GOTO','FLAP'}:
                            self.units[u]['rememberedIndex'] = value
                        if op == 'HOME':
                            self.units[u]['reference'] = 'HALL'
                self.pending = (time.monotonic() + 0.7, complete, job)
        return {'id': job['id']}


def handler_for(model):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def log_message(self, *args):
            pass

        def send_json(self, data, code=200, cookie=None):
            value = json.dumps(data, ensure_ascii=False).encode()
            self.send_response(code)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(value)))
            self.send_header('Cache-Control', 'no-store')
            if cookie is not None:
                self.send_header('Set-Cookie', cookie)
            self.end_headers()
            self.wfile.write(value)

        def logged_in(self):
            return any(f'SFSession={token}' in self.headers.get('Cookie', '').split('; ') for token in model.sessions)

        def do_GET(self):
            path = urlparse(self.path).path
            if path == '/':
                page = (ROOT / 'esp32cam/web/index.html').read_bytes()
                self.send_response(200)
                self.send_header('Content-Type','text/html; charset=utf-8')
                self.send_header('Content-Length',str(len(page)))
                self.end_headers()
                self.wfile.write(page)
                return
            if path == '/api/session':
                self.send_json(dict(authenticated=self.logged_in(),csrf=model.csrf if self.logged_in() else ''))
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
                    self.send_json(dict(schema=2, device='splitflap-esp32s', mega=dict(digits=len(model.schema['units']),calibration=model.schema['calibration']),
                                        display={k:v for k,v in model.display.items() if k not in {'running','reason','timeValid','timeSource','unix'}}))
                elif path == '/':
                    page = (ROOT / 'esp32cam/web/index.html').read_bytes()
                    self.send_response(200)
                    self.send_header('Content-Type', 'text/html; charset=utf-8')
                    self.send_header('Content-Length', str(len(page)))
                    self.end_headers()
                    self.wfile.write(page)
                elif path in {'/wiring-runtime.svg', '/wiring-flashing.svg', '/wiring-mega.svg', '/wiring-hall.svg',
                              '/wiring-single.svg', '/wiring-single-pins.svg'}:
                    files = {'/wiring-runtime.svg': 'esp32cam/wiring-runtime.svg', '/wiring-flashing.svg': 'esp32cam/wiring-flashing.svg',
                             '/wiring-mega.svg': 'mega2560/wiring-ten.svg', '/wiring-hall.svg': 'mega2560/hall-board.svg',
                             '/wiring-single.svg': 'esp32cam/wiring-single.svg', '/wiring-single-pins.svg': 'esp32cam/wiring-single-pins.svg'}
                    image = (ROOT / files[path]).read_bytes()
                    self.send_response(200)
                    self.send_header('Content-Type', 'image/svg+xml; charset=utf-8')
                    self.send_header('Content-Length', str(len(image)))
                    self.end_headers()
                    self.wfile.write(image)
                else:
                    self.send_json({'error': 'not_found'}, 404)

        def do_POST(self):
            path = urlparse(self.path).path
            data = self.rfile.read(min(int(self.headers.get('Content-Length', 0)), 2100000))
            if path == '/api/login':
                values = json.loads(data)
                if values.get('username') == 'admin' and values.get('password') == model.admin_password:
                    token = secrets.token_hex(16)
                    model.sessions.add(token)
                    self.send_json(dict(authenticated=True,csrf=model.csrf),cookie=f'SFSession={token}; HttpOnly; SameSite=Strict; Path=/; Max-Age=34560000')
                else:
                    self.send_json({'error':'帳號或密碼不正確。'},401)
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
                    if path == '/api/logout':
                        model.sessions = {t for t in model.sessions if f'SFSession={t}' not in self.headers.get('Cookie','')}
                        self.send_json({},cookie='SFSession=; Path=/; Max-Age=0')
                    elif path == '/api/command':
                        self.send_json(model.command(body), 202)
                    elif path == '/api/config':
                        settings = copy.deepcopy(body['mega'])
                        units = settings.get('units',list(range(settings.get('digits',1))))
                        settings['units'] = units
                        settings.pop('digits',None)
                        if body.get('schema') not in {1,2} or units != list(range(len(units))) or not 1 <= len(units) <= 10 or len(set(units)) != len(units) or any(type(u) is not int or not 0 <= u <= 9 for u in units) or len(settings['calibration']) != 10:
                            raise ValueError('整份設定驗證失敗。')
                        for c in settings['calibration']:
                            if not 1000 <= c['revolution'] <= 20000 or not 0 <= c['offset'] < c['revolution'] or not 800 <= c['speed'] <= 30000 or not 100 <= c['accel'] <= 6000 or sorted(c['order']) != list('1234'):
                                raise ValueError('校準參數無效。')
                        job, repeated = model.new_job(body, 'CONFIG')
                        if not repeated:
                            previous = model.schema
                            changed_map = previous['units'] != units
                            geometry = ['revolution', 'offset', 'direction', 'order', 'activeLow']
                            for u in model.units:
                                physical = u['unit']
                                if any(previous['calibration'][physical][key] != settings['calibration'][physical][key] for key in geometry):
                                    u.update(reference='NONE', armed=False)
                                    model.workflow['calibrated'] = False
                                if changed_map:
                                    u['armed'] = False
                                    if physical not in previous['units'] or physical not in units:
                                        u['reference'] = 'NONE'
                            model.display.update(running=False, reason='設定已保存，字元保留。')
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
                    elif path == '/api/calibration':
                        action=body['action']
                        if action == 'start' and model.workflow['calibrating']:
                            raise ValueError('已在校準中。')
                        if action != 'start' and not model.workflow['calibrating']:
                            raise ValueError('請先開始校準。')
                        job,repeated=model.new_job(body,'CALIBRATION')
                        if not repeated:
                            if action == 'start':
                                model.original=copy.deepcopy(model.schema)
                                model.workflow['calibrating']=True
                                model.workflow['jogReadyMask']=(1<<len(model.schema['units']))-1
                            model.workflow.update(active=True,message='校準處理中')
                            def complete():
                                if action in {'start','finish'}:
                                    for u in model.schema['units']:
                                        model.units[u].update(reference='HALL',index=0,armed=True)
                                elif action in {'preview','jog'}:
                                    c=model.schema['calibration'][body['unit']]
                                    c['offset']=body['value'] if action=='preview' else (c['offset']+body['value'])%c['revolution']
                                elif action == 'cancel':
                                    model.schema=model.original
                                    model.workflow['calibrating']=False
                                if action == 'finish':
                                    model.workflow.update(calibrated=True,calibrating=False)
                                    if not model.recovery_chosen:
                                        model.display['autoRecovery']=True
                                        model.recovery_chosen=True
                                model.workflow['message']='偏移已永久保存，校準完成。' if action=='finish' else '歸零／微調完成，請確認空白頁後保存。'
                            model.pending=(time.monotonic()+0.5,complete,job)
                        self.send_json({'id':job['id']},202)
                    elif path == '/api/preferences':
                        if body.get('autoRecovery') and not model.workflow['calibrated']:
                            raise ValueError('請先完成校準。')
                        if 'autoRecovery' in body:
                            model.recovery_chosen=True
                        model.display.update({k:v for k,v in body.items() if k not in {'run','boot','request'}})
                        if 'run' in body:
                            model.display['running']=body['run']
                            model.resume_display=body['run']
                        if body.get('run'):
                            for u in model.schema['units']:
                                if model.units[u]['reference']=='NONE' and not model.workflow['calibrated']:
                                    raise ValueError('請先定位或校準。')
                                model.units[u].update(armed=True,reference='HALL' if model.workflow['calibrated'] else 'MANUAL')
                            job,_=model.new_job(body,'START');job['stage']='done'
                            self.send_json({'id':job['id']},202)
                        else:
                            self.send_json({})
                        model.display['reason']='自動顯示執行中。' if model.display['running'] else '自動顯示已暫停。'
                    elif path == '/api/network':
                        if model.pending or model.maintenance:
                            raise ValueError('busy')
                        model.display['running'] = False
                        model.network.update(ssid=body['ssid'], connected=bool(body['ssid']), staIp='192.168.1.50')
                        if body.get('adminPassword'):
                            model.admin_password=body['adminPassword']
                            model.sessions.clear()
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
                            if body.get('powerOn') and model.display['autoRecovery'] and model.workflow['calibrated']:
                                for u in model.schema['units']:
                                    model.units[u].update(reference='HALL',armed=True,index=model.units[u]['rememberedIndex'] or 0)
                                model.display['running']=model.resume_display
                        if body.get('expireSessions'):
                            model.sessions.clear()
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

        def shutdown_request(self, request):
            # Let the client close first. A server-first close after "Connection: close"
            # is reset by some Windows traffic filters (seen with AdGuard) about 1 in 3 times.
            try:
                request.settimeout(2)
                while request.recv(4096):
                    pass
            except OSError:
                pass
            request.close()
    return MockServer(('127.0.0.1', port), handler_for(model)), model


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--single', action='store_true', help='ESP32 + MCP23017 single-controller state')
    args = parser.parse_args()
    server, _model = create_server(args.port)
    _model.single = args.single
    print(f'Mock web http://127.0.0.1:{server.server_port} — admin / MockPassword123 (no hardware)', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
