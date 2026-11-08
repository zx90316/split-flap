#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate purchased Mega Pro / NodeMCU ESP-32S wiring. Follow printed pin names."""
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INK, GND, V12, V5, V3, TX, RX = '#183f36', '#384b47', '#d27915', '#cc3746', '#8357b0', '#2b65c8', '#00866b'


class Svg:
    def __init__(self, width, height, title, subtitle):
        self.items = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
                      f'<title id="title">{escape(title)}</title><desc id="desc">{escape(subtitle)}</desc>',
                      '<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0 0L10 5L0 10z" fill="context-stroke"/></marker></defs>',
                      '<style>text{font-family:"Microsoft JhengHei","Noto Sans CJK TC",sans-serif;fill:#183f36}.wire{fill:none;stroke-linejoin:round;stroke-linecap:round}</style>',
                      f'<rect width="{width}" height="{height}" fill="#f3f6ef"/>']
        self.text(45, 62, title, 30, weight='bold')
        self.text(45, 96, subtitle, 17)

    def text(self, x, y, label, size=17, color=INK, weight='normal', anchor='start'):
        self.items.append(f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{weight}" text-anchor="{anchor}" style="fill:{color}">{escape(str(label))}</text>')

    def box(self, x, y, w, h, title, fill='#fff'):
        self.items.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="13" fill="{fill}" stroke="#bfd0c5" stroke-width="2"/>')
        self.text(x+18, y+34, title, 22, weight='bold')

    def wire(self, points, color, width=3, arrow=False, dash=False):
        attrs = (' marker-end="url(#arrow)"' if arrow else '') + (' stroke-dasharray="8 6"' if dash else '')
        self.items.append(f'<polyline class="wire" points="{" ".join(f"{x},{y}" for x,y in points)}" stroke="{color}" stroke-width="{width}"{attrs}/>')

    def dot(self, x, y, color):
        self.items.append(f'<circle cx="{x}" cy="{y}" r="5" fill="{color}"/>')

    def pin(self, x, y, label, color, side='right'):
        self.dot(x, y, color)
        self.text(x-14 if side == 'right' else x+14, y+6, label, 17, color, anchor='end' if side == 'right' else 'start')

    def note(self, x, y, lines, color=INK, size=17):
        for i, line in enumerate(lines): self.text(x, y + 29*i, line, size, color)

    def save(self, filename):
        (ROOT / filename).write_text('\n'.join(self.items + ['</svg>', '']), encoding='utf-8')


def runtime():
    s = Svg(1440, 1320, '正常運作：Mega2560 Pro ＋ NodeMCU ESP-32S',
            '依針腳名稱接線；方塊為功能示意，不代表模組外觀、排針順序或實際尺寸。')
    s.box(45, 145, 290, 160, '隔離式 12V 電源')
    s.note(65, 218, ['正端經 F1 保險絲／主開關', '負端接共地分配點'])
    s.pin(335, 185, '+12V', V12); s.pin(335, 280, 'GND', GND)
    s.box(565, 165, 320, 150, '現有 LM2596 → 5.0V')
    s.pin(565, 220, 'IN+', V12, 'left'); s.pin(885, 225, 'OUT+ 5V', V5)
    s.text(585, 275, 'IN−／OUT− 接共地', 17, GND)
    s.box(1080, 145, 310, 160, '馬達 12V 分支')
    s.note(1100, 214, ['獨立開關 → 各 ULN + / COM', '各 ULN GND 回共地分配點', '馬達／Hall 接線沿用 Mega 圖'])
    s.wire([(335,185),(440,185),(440,130),(1055,130),(1055,185),(1080,185)], V12, 4, True)
    s.wire([(440,185),(440,220),(565,220)], V12, 4, True); s.dot(440,185,V12)
    s.text(1035, 353, 'LM2596 → F2 → 5V 分配', 20, V5, weight='bold')
    s.text(1035, 380, '正常運作時兩板電腦 USB 拔除', 17)
    s.wire([(885,225),(945,225),(945,260)], V5, 4)
    s.items.append('<rect x="935" y="260" width="20" height="40" rx="3" fill="#fff" stroke="#cc3746" stroke-width="3"/>')
    s.text(968,287,'F2',18,V5,weight='bold')
    s.wire([(945,300),(945,390),(1210,390),(1210,455)], V5, 4, True)
    s.wire([(945,335),(30,335),(30,505),(65,505)], V5, 4, True); s.dot(945,335,V5)
    s.text(55, 369, '控制 5V 分支：關閉馬達分支時保持供電', 18, V5)
    s.box(65, 455, 395, 465, 'NodeMCU ESP-32S', '#edf3ff')
    s.text(85, 540, 'ESP-WROOM-32／4MB／無 PSRAM', 17)
    s.pin(65,505,'5V 輸入',V5,'left')
    s.pin(460,590,'GPIO13 / TX2',TX)
    s.pin(460,660,'GPIO14 / RX2',RX)
    s.pin(460,760,'3V3 輸出',V3)
    s.text(85, 835, '板上 USB：直接燒錄與監看', 18)
    s.text(85, 870, '5V 輸入腳依實板核對；3V3 不接 5V', 18)
    s.pin(260,920,'GND',GND)
    s.box(585, 485, 300, 405, 'P011 8 路電平轉換', '#f5effa')
    s.text(602, 548, 'A 側 3.3V ／ B 側 5V', 18)
    s.pin(585,590,'A0',TX,'left'); s.pin(885,590,'B0',TX)
    s.pin(585,660,'A1',RX,'left'); s.pin(885,660,'B1',RX)
    s.pin(585,760,'VCCA',V3,'left')
    s.pin(885,760,'VCCB',V5); s.pin(735,890,'GND',GND)
    s.text(602, 817, 'A2–A7 / B2–B7：不接', 17)
    s.text(602, 850, '沿用現有 MOS 板，沒有 OE', 17)
    s.box(1035, 455, 355, 465, 'Mega2560 Pro · CH340G', '#e7f3ec')
    s.text(1055,510,'5V 腳：接穩壓後的 5.0V',18,V5)
    s.text(1055,548,'板上 USB：燒錄／調機時接電腦',17)
    s.pin(1035,590,'D19 / RX1',TX,'left'); s.pin(1035,660,'D18 / TX1',RX,'left')
    s.pin(1035,760,'5V 電源節點 → VCCB',V5,'left')
    s.text(1055,835,'邏輯腳沿用原表；實體排位不同',17); s.text(1055,870,'Pro 公排針使用母頭線',17)
    s.pin(1210,920,'GND',GND)
    s.wire([(460,590),(585,590)],TX,3,True); s.wire([(885,590),(1035,590)],TX,3,True)
    s.wire([(1035,660),(885,660)],RX,3,True); s.wire([(585,660),(460,660)],RX,3,True)
    s.wire([(460,760),(585,760)],V3,3,True)
    s.wire([(1035,760),(885,760)],V5,3,True)
    s.wire([(335,280),(370,280),(370,320),(15,320),(15,960)],GND,4)
    s.wire([(15,960),(1340,960)],GND,4)
    s.wire([(700,315),(700,352)],GND,3)
    s.wire([(370,320),(700,320)],GND,3); s.dot(370,320,GND); s.dot(700,320,GND)
    s.wire([(780,315),(780,340),(700,340)],GND,3)
    s.dot(700,340,GND)
    for y,w in [(352,26),(360,18),(368,10)]: s.wire([(700-w/2,y),(700+w/2,y)],GND,2)
    s.text(715,365,'GND 分配點',15,GND)
    s.wire([(670,590),(785,590)],TX,2,True)
    s.wire([(785,660),(670,660)],RX,2,True)
    for x,y in [(260,920),(735,890),(1210,920)]: s.wire([(x,y),(x,960)],GND,3); s.dot(x,960,GND)
    s.text(55,1003,'共地：12V−、LM2596 IN−／OUT−、Mega、ESP32、P011、ULN、Hall；+12V 與 +5V 分開。',18,GND)
    s.box(45,1030,665,225,'新購配件與配電')
    s.note(65,1090,['銅芯直徑 0.5mm ≈ 0.196mm²／24AWG。',
      '短訊號／驗證後的單組支路用線，不作馬達總線。',
      'KF301 2P ×4 需焊 PCB／分配線，不是完成的母線。',
      '4位 DIP ×4 暫備用；高溫膠帶僅輔助絕緣。',
      'F1／F2 額定待選；購買選項 5A 不代表適用值。'],size=17)
    s.box(735,1030,655,225,'任一板改接電腦 USB')
    s.note(755,1090,['① 關馬達與控制電源；先拔 UART 四條訊號線。',
      '② 拔該板外部 5V，才接該板的電腦 USB。',
      '③ 完成後拔 USB、關控制電源，再復原 5V／UART。',
      '④ 兩板重新供電；重新定位並 ARM 才可轉動。',
      '繼電器 IN 電平未確認，暫不接；保留手動馬達開關。'],size=17)
    s.text(45,1293,'線色：橘=12V　紅=5V　紫=3.3V　藍=ESP→Mega　綠=Mega→ESP　灰黑=共地；無圓點的交叉線不相接。',17)
    s.save('wiring-runtime.svg')



def flashing():
    s = Svg(1300, 1180, 'USB 燒錄：兩塊新主控各自使用板上 USB',
            '不用外接 CP2102；USB 插頭依到貨實板選擇。關馬達、拔外部 5V 與板間訊號線，再接電腦。')
    s.box(65,145,1115,145,'先整理供電','#fff0f1')
    s.note(85,207,['12V 馬達支路及 LM2596 控制電源關閉；兩板外部 5V 線實際拔除。',
      'P011 A0／A1／B0／B1 四條訊號線拔除，GND 共地；不可並接外部 5V 與 USB。',
      '可以只接需要燒錄的一板；另一板保持斷電與訊號分離，不向未供電的 GPIO 回灌。'])
    s.box(65,335,1115,90,'電腦：兩條獨立 USB 資料線／兩個不同 COM 埠','#e7f3ec')
    s.box(65,510,510,345,'NodeMCU ESP-32S','#edf3ff')
    s.box(670,510,510,345,'Mega2560 Pro · CH340G','#e7f3ec')
    s.wire([(320,425),(320,510)],TX,5,True)
    s.wire([(925,425),(925,510)],RX,5,True)
    s.text(85,470,'板上 USB：5V＋資料',18,TX)
    s.text(690,470,'板上 USB：5V＋資料',18,RX)
    s.note(85,585,['VS Code：ESP32-S: 建置＋燒錄＋串列監看',
      'classic ESP32／4MB／PSRAM disabled',
      '支援 OTA 的 min_spiffs 分割配置',
      '通常由板上 USB-UART 自動重設。',
      'Connecting 卡住：按住 BOOT，',
      '按一下 EN／RESET，開始寫入再放開。',
      '115200；ACCESS 顯示 Wi-Fi／登入資料。'])
    s.note(690,585,['VS Code：MEGA: 建置＋燒錄＋串列除錯',
      'Arduino Mega or Mega 2560',
      'ATmega2560／16MHz；沿用現有 bootloader',
      'COM 埠重新查詢，不沿用舊板號碼。',
      '沒有 bootloader 的板子需另行處理。',
      '115200；STATUS ALL 查各位狀態。',
      '新板 EEPROM 不含舊 Mega 的校準。'])
    s.box(65,900,1115,215,'完成後復原')
    s.note(85,961,['停止監看 → 拔電腦 USB → 確認控制電源關閉 → 復原穩壓 5V／共地與四條 UART 訊號線。',
      '重新供電，以 STATUS 確認 Mega=connected；核對實物定位、ARM 後才開馬達分支。',
      '新 ESP32 的 Wi-Fi、登入密碼及字元歷史須重新設定，不會從燒毀的舊板自動移入。',
      '.sffw 僅更新 NodeMCU ESP-32S；Mega 使用 USB。舊 splitflap-esp32cam.sffw 不適用。'])
    s.save('wiring-flashing.svg')


SDA, SCL, RST = TX, RX, '#b04a9e'


def ground(s, x, y, color=GND):
    """Ground flag: every one of these joins the same common-ground bus."""
    s.wire([(x, y), (x, y + 16)], color, 3)
    for i, half in enumerate((14, 9, 4)):
        s.wire([(x - half, y + 16 + 6*i), (x + half, y + 16 + 6*i)], color, 3)


def single():
    s = Svg(1500, 1480, '單主控：NodeMCU ESP-32S ＋ 4 片 MCP23017（不接 Mega）',
            '依模組印字接線；方塊為功能示意。所有接地符號接同一共地母線。I²C 主線盡量短，MCP 可靠近各自的 ULN。')
    # Power
    s.box(45, 140, 300, 150, '隔離式 12V 電源')
    s.note(65, 212, ['正端經 F1 保險絲／主開關', '負端接 GND 共地'])
    s.pin(345, 180, '+12V', V12); s.pin(345, 262, 'GND', GND); ground(s, 345, 262)
    s.box(520, 150, 300, 130, 'LM2596 → 5.0V')
    s.pin(520, 200, 'IN+', V12, 'left'); s.pin(820, 200, 'OUT+', V5)
    s.text(540, 262, 'IN−／OUT− 接 GND 共地', 17, GND)
    s.box(1150, 140, 305, 200, '馬達 12V 分支', '#fff4e6')
    s.note(1170, 210, ['手動開關（必須保留）', '→ 各 ULN2003 的 +', '12V 不接 ESP32／MCP／霍爾', 'ULN GND 直接回共地點'])
    s.wire([(345, 180), (440, 180), (440, 200), (520, 200)], V12, 4, True); s.dot(440, 180, V12)
    s.wire([(440, 180), (440, 118), (1120, 118), (1120, 190), (1150, 190)], V12, 4, True)
    s.wire([(820, 200), (920, 200), (920, 232)], V5, 4)
    s.items.append('<rect x="910" y="232" width="20" height="40" rx="3" fill="#fff" stroke="#cc3746" stroke-width="3"/>')
    s.text(940, 258, 'F2', 18, V5, weight='bold')
    s.wire([(920, 272), (920, 1250)], V5, 5)
    s.wire([(920, 330), (25, 330), (25, 470), (45, 470)], V5, 4); s.dot(920, 330, V5)
    s.text(60, 362, '5V 控制分配：關閉馬達 12V 時仍供電', 18, V5, weight='bold')
    # ESP32
    s.box(45, 420, 330, 440, 'NodeMCU ESP-32S', '#edf3ff')
    s.pin(45, 470, '5V 輸入', V5, 'left')
    s.pin(375, 520, 'GPIO21 · SDA', SDA); s.pin(375, 590, 'GPIO22 · SCL', SCL)
    s.pin(375, 660, 'GPIO23 · RST', RST); s.pin(375, 750, '3V3', V3)
    s.note(65, 790, ['板上 USB：燒錄／監看', '（接 USB 前拔外部 5V）'], size=16)
    s.pin(210, 860, 'GND', GND, 'left'); ground(s, 210, 860)
    # Level shifter
    s.box(500, 440, 250, 400, 'P011 電位轉換', '#f5effa')
    s.note(518, 500, ['A 側 3.3V ↔ B 側 5V'], size=16)
    for y, n, c in ((520, 0, SDA), (590, 1, SCL), (660, 2, RST)):
        s.pin(500, y, f'A{n}', c, 'left'); s.pin(750, y, f'B{n}', c)
        s.wire([(375, y), (500, y)], c, 4)
    s.pin(500, 750, 'VCCA', V3, 'left'); s.pin(750, 750, 'VCCB', V5)
    s.wire([(375, 750), (500, 750)], V3, 4)
    s.wire([(750, 750), (920, 750)], V5, 4); s.dot(920, 750, V5)
    s.note(518, 795, ['A3–A7／B3–B7 留空'], size=16)
    s.pin(625, 840, 'GND', GND, 'left'); ground(s, 625, 840)
    s.text(518, 900, '⚠ 10/31 冒煙過：先檢查或更換，', 16, V5)
    s.text(518, 926, '且須為 MOS（BSS138 類）轉換板', 16, V5)
    # I2C bus trunks
    for x, y, c in ((830, 520, SDA), (860, 590, SCL), (890, 660, RST)):
        s.wire([(750, y), (x, y)], c, 4); s.dot(x, y, c)
    rows = []
    for chip in range(4):
        y0 = 400 + chip*235
        last = chip == 3
        s.box(990, y0, 250, 205, f'MCP23017 · 0x2{chip}', '#e7f3ec' if not last else '#fff7d6')
        for k, (label, color, bus) in enumerate((('SDA', SDA, 830), ('SCL', SCL, 860), ('RST', RST, 890), ('VCC 5V', V5, 920))):
            y = y0 + 60 + 30*k
            s.wire([(bus, y), (990, y)], color, 3); s.dot(bus, y, color)
            s.text(1004, y + 6, label, 15, color)
        s.text(1004, y0 + 186, 'GND', 15, GND); s.wire([(990, y0 + 180), (965, y0 + 180)], GND, 3); ground(s, 965, y0 + 180)
        bits = [(chip >> b) & 1 for b in (2, 1, 0)]
        s.text(1225, y0 + 70, 'A2 A1 A0', 15, INK, anchor='end')
        s.text(1225, y0 + 94, '  '.join('VCC' if b else 'GND' for b in bits), 14, INK, anchor='end')
        s.text(1225, y0 + 124, 'ITA／ITB 不接', 14, GND, anchor='end')
        rows.append(y0)
    s.wire([(830, 520), (830, rows[-1] + 60)], SDA, 4)
    s.wire([(860, 590), (860, rows[-1] + 90)], SCL, 4)
    s.wire([(890, 660), (890, rows[-1] + 120)], RST, 4)
    # Outputs
    groups = [[('GPA0–3', 'U0'), ('GPA4–7', 'U1'), ('GPB0–3', 'U2'), ('GPB4–7', 'U3')],
              [('GPA0–3', 'U4'), ('GPA4–7', 'U5'), ('GPB0–3', 'U6'), ('GPB4–7', 'U7')],
              [('GPA0–3', 'U8'), ('GPA4–7', 'U9')]]
    for chip, group in enumerate(groups):
        for i, (pins, unit) in enumerate(group):
            y = rows[chip] + 30 + 46*i
            s.wire([(1240, y), (1312, y)], INK, 6)
            s.text(1276, y - 8, pins, 13, INK, anchor='middle')
            s.items.append(f'<rect x="1312" y="{y-17}" width="160" height="34" rx="7" fill="#fff4e6" stroke="#d27915" stroke-width="2"/>')
            s.text(1392, y + 6, f'{unit} ULN IN1–4', 16, INK, weight='bold', anchor='middle')
    s.text(1392, rows[2] + 140, 'GPB0–7 不接', 15, GND, anchor='middle')
    y = rows[3] + 75
    s.wire([(1240, y), (1312, y)], INK, 9)
    s.text(1276, y - 10, '10 條', 13, INK, anchor='middle')
    s.items.append(f'<rect x="1312" y="{y-38}" width="160" height="76" rx="7" fill="#fff7d6" stroke="#b08a00" stroke-width="2"/>')
    s.text(1392, y - 8, 'KY-003 × 10', 16, INK, weight='bold', anchor='middle')
    s.text(1392, y + 18, 'OUT（訊號）', 15, INK, anchor='middle')
    s.note(1258, rows[3] + 150, ['GPB0–4 → U0–U4', 'GPA0–4 → U5–U9', '霍爾 + 接 5V'], size=15)
    # Legend and rules
    ly = 1385
    for i, (label, color) in enumerate((('12V', V12), ('5V', V5), ('3.3V', V3), ('SDA', SDA), ('SCL', SCL), ('RST', RST), ('GND', GND))):
        x = 45 + i*130
        s.wire([(x, ly), (x + 44, ly)], color, 5); s.text(x + 54, ly + 6, label, 16, color, weight='bold')
    s.note(45, 1430, ['粗線 = 4 條線（IN1–IN4），依 GPx0→IN1 … GPx3→IN4；霍爾 10 條。細節見 wiring-single-pins.svg。',
                     '1～4 位只需 0x20＋0x23；5～8 位加 0x21；9～10 位再加 0x22。原 GPIO13／14 的 UART 線全部拆除。'], size=16)
    s.save('wiring-single.svg')


def single_pins():
    s = Svg(1500, 1240, '單主控逐腳對照：MCP23017 → ULN2003 IN／KY-003',
            '同一片的四顆馬達合併成一筆 16-bit 寫入；IN 順序接錯可在網頁用 ORDER／DIR 校正，不必改線。')
    colors = ['#2b65c8', '#00866b', '#d27915', '#b04a9e']
    units = ['0x20→U0　0x21→U4　0x22→U8', '0x20→U1　0x21→U5　0x22→U9',
             '0x20→U2　0x21→U6　0x22 不接', '0x20→U3　0x21→U7　0x22 不接']
    # Motor chip: four IN1..IN4 groups, one ULN2003 each.
    s.box(45, 140, 700, 990, '馬達用 MCP（0x20／0x21／0x22）', '#e7f3ec')
    s.items.append('<rect x="85" y="200" width="150" height="900" rx="10" fill="#fff" stroke="#183f36" stroke-width="2"/>')
    s.text(160, 228, 'MCP23017', 17, INK, weight='bold', anchor='middle')
    for p in range(16):
        group = p // 4
        y = 300 + 40*p + 62*group
        name = f'GP{"AB"[p // 8]}{p % 8}'
        if p % 4 == 0:
            top = y - 50
            s.items.append(f'<rect x="400" y="{top}" width="330" height="{40*3 + 72}" rx="9" fill="#fff4e6" stroke="{colors[group]}" stroke-width="2"/>')
            s.text(414, top + 26, f'ULN2003 #{group + 1}：{units[group]}', 13, INK, weight='bold')
        s.text(220, y + 6, name, 16, colors[group], weight='bold', anchor='end')
        s.wire([(235, y), (400, y)], colors[group], 3); s.dot(235, y, colors[group]); s.dot(400, y, colors[group])
        s.text(418, y + 6, f'IN{p % 4 + 1}', 16, colors[group], weight='bold')
    # Hall chip
    s.box(800, 140, 655, 640, '霍爾用 MCP（0x23）', '#fff7d6')
    s.items.append('<rect x="840" y="200" width="150" height="490" rx="10" fill="#fff" stroke="#183f36" stroke-width="2"/>')
    s.text(915, 228, 'MCP23017', 17, INK, weight='bold', anchor='middle')
    for p in range(16):
        y = 255 + 27*p
        name = f'GP{"AB"[p // 8]}{p % 8}'
        unit = p - 8 if 8 <= p <= 12 else p + 5 if p <= 4 else None  # GPB0-4: U0-U4, GPA0-4: U5-U9
        color = INK if unit is not None else '#9aa8a3'
        s.text(975, y + 5, name, 15, color, weight='bold', anchor='end')
        if unit is None:
            s.text(1010, y + 5, '不接（只能當輸出）' if p in (7, 15) else '不接', 14, color)
        else:
            s.wire([(990, y), (1150, y)], '#b08a00', 3); s.dot(990, y, '#b08a00')
            s.text(1160, y + 5, f'U{unit} KY-003 OUT', 15, INK)
    s.note(820, 728, ['KY-003：+ 接 5V、− 接 GND；低電位＝磁鐵到位。', 'MCP 內建上拉已由韌體開啟，不必外加電阻。'], size=15)
    # Module header
    s.box(800, 820, 655, 230, 'MCP23017 模組排針（四片相同）', '#edf3ff')
    rows = [('VCC', '5V 控制分配', V5), ('GND', '共地', GND), ('SDA', 'P011 B0 ← A0 ← ESP GPIO21', SDA),
            ('SCL', 'P011 B1 ← A1 ← ESP GPIO22', SCL), ('RST', 'P011 B2 ← A2 ← ESP GPIO23（低＝重設）', RST),
            ('ITA／ITB', '不接', GND), ('A0 A1 A2', '依地址接 GND／VCC，不可浮接', INK)]
    for i, (pin, to, color) in enumerate(rows):
        y = 885 + 23*i
        s.text(830, y, pin, 15, color, weight='bold'); s.text(950, y, to, 15, INK)
    s.text(45, 1175, '地址：0x20 ＝ A2 A1 A0 全接 GND；0x21 ＝ 只有 A0 接 VCC；0x22 ＝ 只有 A1 接 VCC；0x23 ＝ A1、A0 接 VCC。', 17, INK, weight='bold')
    s.text(45, 1205, '12V 只進 ULN2003 的 +；MCP、霍爾、P011 B 側都在 5V；ESP32 與 P011 A 側在 3.3V。', 17, V5)
    s.save('wiring-single-pins.svg')


if __name__ == '__main__':
    runtime(); flashing(); single(); single_pins()
    print('Generated dual-controller runtime/USB and single-controller wiring diagrams.')
