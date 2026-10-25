#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Add the ESP32 workflows without replacing existing Mega configurations."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VS = ROOT / '.vscode'


def read(name):
    return json.loads((VS / name).read_text(encoding='utf-8'))


def save(name, value):
    (VS / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


actions = [('準備環境', 'setup'), ('檢查環境', 'doctor'), ('查詢連接埠', 'ports'), ('建置韌體', 'build'),
           ('建置＋燒錄', 'upload'), ('建置＋燒錄＋串列監看', 'upload-monitor'), ('串列監看', 'monitor'),
           ('全部模擬測試', 'test'), ('手機網頁流程測試', 'test-web'), ('產生接線圖', 'diagrams'), ('開啟網頁模擬器', 'mock-web'), ('建置主機偵錯', 'native-debug')]
tasks = read('tasks.json')
launch = read('launch.json')
for title, action in actions:
    label = 'ESP32-CAM: ' + title
    arguments = [action]
    if action in {'upload', 'upload-monitor', 'monitor'}:
        arguments += ['--port', '${input:esp32CamPort}']
    if not any(t.get('label') == label for t in tasks['tasks']):
        tasks['tasks'].append(dict(label=label, type='process', command='${command:python.interpreterPath}',
            args=['${workspaceFolder}/esp32cam/tools/dev.py'] + arguments,
            options=dict(cwd='${workspaceFolder}', env=dict(PYTHONUTF8='1', PYTHONUNBUFFERED='1')),
            problemMatcher=['$gcc'], presentation=dict(reveal='always', panel='dedicated', clear=True),
            runOptions=dict(instanceLimit=1)))
    if action in {'setup', 'doctor', 'ports', 'build', 'upload-monitor', 'monitor', 'test', 'test-web', 'mock-web'} and not any(t.get('name') == label for t in launch['configurations']):
        launch['configurations'].append(dict(name=label, type='debugpy', request='launch',
            program='${workspaceFolder}/esp32cam/tools/dev.py', args=arguments, cwd='${workspaceFolder}',
            console='integratedTerminal', justMyCode=True, subProcess=False,
            env=dict(PYTHONUTF8='1', PYTHONUNBUFFERED='1'), internalConsoleOptions='neverOpen',
            presentation=dict(group='3-ESP32-CAM', order=len(launch['configurations']))))
for config in [tasks, launch]:
    if not any(i['id'] == 'esp32CamPort' for i in config['inputs']):
        config['inputs'].append(dict(id='esp32CamPort', type='promptString', default='COM4',
            description='輸入已確認的 CP2102 COM 埠（不是 Mega COM3）；GPIO0 接 GND 後 RESET；馬達分支關閉'))
if not any(c.get('name') == '主機中斷點: ESP32↔Mega 通訊與網頁命令' for c in launch['configurations']):
    launch['configurations'].append(dict(name='主機中斷點: ESP32↔Mega 通訊與網頁命令', type='cppvsdbg', request='launch',
        program='${workspaceFolder}/esp32cam/test/.build/test_bridge_debug.exe', args=[], cwd='${workspaceFolder}',
        console='internalConsole', stopAtEntry=True, preLaunchTask='ESP32-CAM: 建置主機偵錯',
        symbolSearchPath='${workspaceFolder}/esp32cam/test/.build', requireExactSource=True,
        presentation=dict(group='4-ESP32通訊主機偵錯', order=1)))
save('tasks.json', tasks)
save('launch.json', launch)
cpp = read('c_cpp_properties.json')
for config in cpp['configurations']:
    paths = config.setdefault('includePath', [])
    for path in ['${workspaceFolder}/libraries/SplitFlapLink/src']:
        if path not in paths:
            paths.append(path)
    if config['name'] == 'Mega 2560 AVR':
        path = '${workspaceFolder}/.tools/arduino-data/packages/arduino/hardware/avr/1.8.8/libraries/EEPROM/src'
        if path not in paths:
            paths.append(path)
if not any(c['name'] == 'ESP32-CAM' for c in cpp['configurations']):
    cpp['configurations'].append(dict(name='ESP32-CAM',
        compilerPath='${workspaceFolder}/.tools/arduino-data/packages/esp32/tools/esp-x32/2601/bin/xtensa-esp32-elf-g++.exe',
        compileCommands='${workspaceFolder}/esp32cam/.build/compile_commands.json',
        includePath=['${workspaceFolder}/esp32cam/controller', '${workspaceFolder}/libraries/SplitFlapLink/src',
            '${workspaceFolder}/.tools/arduino-data/packages/esp32/hardware/esp32/3.3.12/cores/esp32'],
        defines=['ARDUINO=10819', 'ARDUINO_ARCH_ESP32', 'ESP32', 'BOARD_HAS_PSRAM', 'F_CPU=240000000L'],
        cStandard='c11', cppStandard='c++17'))
save('c_cpp_properties.json', cpp)
print('VS Code ESP32-CAM tasks, launches and IntelliSense added; Mega configurations retained.')
