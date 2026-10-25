#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make ESP32-CAM/MCP the primary workflow; retain historical Mega tools."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VS = ROOT / '.vscode'


def read(name):
    return json.loads((VS / name).read_text(encoding='utf-8'))


def save(name, value):
    (VS / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


actions = [('建置韌體', 'build'), ('建置＋燒錄＋串列監看', 'upload-monitor'), ('串列監看', 'monitor'),
           ('查詢連接埠', 'ports'), ('準備環境', 'setup'), ('檢查環境', 'doctor'),
           ('建置＋燒錄', 'upload'), ('重新建置韌體', 'rebuild'), ('全部模擬測試', 'test'),
           ('手機網頁流程測試', 'test-web'), ('產生接線圖', 'diagrams'), ('開啟網頁模擬器', 'mock-web'),
           ('準備主機偵錯工具', 'setup-host'), ('建置主機偵錯', 'native-debug')]
tasks, launch = read('tasks.json'), read('launch.json')
old_tasks = [t for t in tasks['tasks'] if not t.get('label', '').startswith('ESP32-CAM:')]
for task in old_tasks:
    if isinstance(task.get('group'), dict):
        task['group'].pop('isDefault', None)
old_launch = [c for c in launch['configurations'] if not c.get('name', '').startswith('ESP32-CAM:')
              and c.get('name') != '主機中斷點: MCP 單主控與運動']
for order, config in enumerate(old_launch):
    config['presentation'] = dict(group='9-歷史雙主控工具', order=order)
primary_tasks, primary_launch = [], []
for order, (title, action) in enumerate(actions):
    label = 'ESP32-CAM: ' + title
    arguments = [action]
    if action in {'upload', 'upload-monitor', 'monitor'}:
        arguments += ['--port', '${input:esp32CamPort}']
    task = dict(label=label, type='process', command='${command:python.interpreterPath}',
        args=['${workspaceFolder}/esp32cam/tools/dev.py'] + arguments,
        options=dict(cwd='${workspaceFolder}', env=dict(PYTHONUTF8='1', PYTHONUNBUFFERED='1')),
        problemMatcher=['$gcc'], presentation=dict(reveal='always', panel='dedicated', clear=True),
        runOptions=dict(instanceLimit=1))
    if action == 'build':
        task['group'] = dict(kind='build', isDefault=True)
    if action in {'test', 'test-web'}:
        task['group'] = 'test'
    primary_tasks.append(task)
    if action != 'native-debug':
        primary_launch.append(dict(name=label, type='debugpy', request='launch',
            program='${workspaceFolder}/esp32cam/tools/dev.py', args=arguments, cwd='${workspaceFolder}',
            console='integratedTerminal', justMyCode=True, subProcess=False,
            env=dict(PYTHONUTF8='1', PYTHONUNBUFFERED='1'), internalConsoleOptions='neverOpen',
            presentation=dict(group='1-ESP32-CAM 單主控', order=order)))
primary_launch.append(dict(name='主機中斷點: MCP 單主控與運動', type='cppvsdbg', request='launch',
    program='${workspaceFolder}/esp32cam/test/.build/test_local_debug.exe', args=[], cwd='${workspaceFolder}',
    console='internalConsole', stopAtEntry=True, preLaunchTask='ESP32-CAM: 建置主機偵錯',
    symbolSearchPath='${workspaceFolder}/esp32cam/test/.build', requireExactSource=True,
    presentation=dict(group='2-單主控模擬偵錯', order=1)))
tasks['tasks'] = primary_tasks + old_tasks
launch['configurations'] = primary_launch + old_launch
for config in [tasks, launch]:
    config['inputs'] = [i for i in config.get('inputs', []) if i['id'] != 'esp32CamPort']
    config['inputs'].append(dict(id='esp32CamPort', type='promptString', default='COM4',
        description='輸入已確認的 CP2102 COM 埠；關馬達分支，GPIO0 接 GND 後 RESET；CP2102 供電腳不接'))
save('tasks.json', tasks)
save('launch.json', launch)
cpp = read('c_cpp_properties.json')
for config in cpp['configurations']:
    paths = config.setdefault('includePath', [])
    for path in ['${workspaceFolder}/libraries/SplitFlapLink/src', '${workspaceFolder}/libraries/SplitFlapMotion/src']:
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
cpp['configurations'].sort(key=lambda c: c['name'] != 'ESP32-CAM')
save('c_cpp_properties.json', cpp)
print('VS Code: ESP32-CAM/MCP is the default build; historical Mega workflows retained.')
