#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate the two reviewable wiring SVGs. Coordinates are functional, not pin pitch."""
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
    s = Svg(1440, 1320, '正常運作：12V 系統供電＋ESP32-CAM ↔ Mega',
            '依針腳名稱接線；方塊為功能示意，不代表模組外觀、排針順序或實際尺寸。')
    s.box(45, 145, 290, 160, '隔離式 12V 電源')
    s.note(65, 218, ['正端經保險絲／主開關', '負端接共地分配點'])
    s.pin(335, 185, '+12V', V12); s.pin(335, 280, 'GND', GND)
    s.box(565, 165, 320, 150, '現有 LM2596 → 5.0V')
    s.pin(565, 220, 'IN+', V12, 'left'); s.pin(885, 225, 'OUT+ 5V', V5)
    s.text(585, 275, 'IN−／OUT− 接共地', 17, GND)
    s.box(1080, 145, 310, 160, '馬達 12V 分支')
    s.note(1100, 214, ['獨立開關 → 各 ULN + / COM', '各 ULN GND 回共地分配點', '馬達／Hall 接線沿用 Mega 圖'])
    s.wire([(335,185),(440,185),(440,130),(1055,130),(1055,185),(1080,185)], V12, 4, True)
    s.wire([(440,185),(440,220),(565,220)], V12, 4, True); s.dot(440,185,V12)
    s.box(1035, 355, 355, 70, '5V USB-A 母座電源小板', '#fff0f1')
    s.text(1055, 412, '原 USB 線 → Mega USB-B', 16)
    s.pin(1390,412,'GND',GND)
    s.wire([(1390,412),(1420,412),(1420,960),(1340,960)],GND,3)
    s.wire([(885,225),(945,225),(945,390),(1035,390)], V5, 4, True)
    s.wire([(945,335),(30,335),(30,505),(65,505)], V5, 4, True); s.dot(945,335,V5)
    s.text(55, 369, '控制 5V 分支：關閉馬達分支時保持供電', 18, V5)
    s.box(65, 455, 395, 465, 'AI-Thinker ESP32-CAM', '#edf3ff')
    s.text(85, 540, '相機／SD 卡／閃光燈不啟用', 17)
    s.pin(65,505,'5V 輸入',V5,'left')
    s.pin(460,590,'GPIO13 / TX2',TX)
    s.pin(460,660,'GPIO14 / RX2',RX)
    s.pin(460,760,'3V3 輸出',V3)
    s.text(85, 835, 'GPIO0：正常運作不接 GND', 18)
    s.text(85, 870, 'U0R / U0T 留給 CP2102', 18)
    s.pin(260,920,'GND',GND)
    s.box(585, 485, 300, 405, 'TXU0202 電平轉換', '#f5effa')
    s.text(602, 548, 'A 側 3.3V ／ B 側 5V', 18)
    s.pin(585,590,'A1 (5)',TX,'left'); s.pin(885,590,'B1Y (8)',TX)
    s.pin(585,660,'A2Y (4)',RX,'left'); s.pin(885,660,'B2 (1)',RX)
    s.pin(585,760,'VCCA (3)',V3,'left'); s.pin(585,815,'OE (6)',V3,'left')
    s.pin(885,760,'VCCB (7)',V5); s.pin(735,890,'GND (2)',GND)
    s.box(1035, 455, 355, 465, 'Arduino Mega 2560', '#e7f3ec')
    s.wire([(1210,425),(1210,455)],V5,4,True); s.text(1060,510,'USB-B：供電／電腦燒錄',18)
    s.pin(1035,590,'D19 / RX1',TX,'left'); s.pin(1035,660,'D18 / TX1',RX,'left')
    s.pin(1035,760,'5V 輸出（只供轉換板）',V5,'left')
    s.text(1055,835,'馬達腳位／Hall 腳位維持原表',17); s.text(1055,870,'VIN／DC 圓孔／外部 5V 不接',17)
    s.pin(1210,920,'GND',GND)
    s.wire([(460,590),(585,590)],TX,3,True); s.wire([(885,590),(1035,590)],TX,3,True)
    s.wire([(1035,660),(885,660)],RX,3,True); s.wire([(585,660),(460,660)],RX,3,True)
    s.wire([(460,760),(585,760)],V3,3,True); s.wire([(525,760),(525,815),(585,815)],V3,3,True); s.dot(525,760,V3)
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
    s.text(55,1003,'共地：12V−、LM2596 IN−／OUT−、Mega、ESP32、ULN、Hall、CP2102 GND',18,GND)
    s.box(45,1030,665,225,'電平轉換板補充')
    s.note(65,1090,['腳號依 TXU0202DCU（8-pin VSSOP）資料表。',
      'C1 100nF：VCCA (3) ↔ GND (2)，靠近晶片。',
      'C2 100nF：VCCB (7) ↔ GND (2)，靠近晶片。',
      '使用已焊接轉接板／模組；不能直接把裸 IC 插杜邦線。',
      '兩板 UART 都經轉換；不要把 Mega 5V TX 接 ESP32。'],size=17)
    s.box(735,1030,655,225,'Mega 改接電腦燒錄')
    s.note(755,1090,['① 關閉馬達 12V 分支，保留控制 5V。',
      '② 同一條 USB 線的 A 端從 5V 母座拔下，改插電腦。',
      '③ Mega 不同時接外部 5V 輸入；不要使用 Y 型電源線。',
      '④ ESP32 仍由 LM2596 供電；CP2102 接法見另一張圖。',
      'LM2596 先空載調至 5.0V，再量兩板 Wi-Fi 負載時的壓降。'],size=17)
    s.text(45,1293,'線色：橘=12V　紅=5V　紫=3.3V　藍=ESP→Mega　綠=Mega→ESP　灰黑=共地；無圓點的交叉線不相接。',17)
    s.save('wiring-runtime.svg')


def flashing():
    s = Svg(1300, 1180, 'CP2102 燒錄：UART0 資料線＋外部控制 5V',
            '只適用 3.3V UART 邏輯的 CP2102 轉接板；依 TXD／RXD／GND 印字，不能照排針位置猜。')
    s.box(65,155,470,145,'12V → LM2596 → 5.0V', '#fff0f1')
    s.note(85,221,['馬達 12V 分支保持關閉', '控制分支供應 ESP32；CP2102 不供電'])
    s.box(65,350,360,70,'電腦 USB → CP2102')
    s.box(65,460,360,350,'CP2102 USB-UART', '#e7f3ec')
    s.wire([(245,420),(245,460)],GND,4)
    s.pin(425,550,'TXD（3.3V 邏輯）',TX)
    s.pin(425,635,'RXD',RX)
    s.pin(245,810,'GND',GND)
    s.text(85,720,'5V、3V3、VCC：都不接',18,V5)
    s.text(85,760,'DTR／RTS／RST：不接',18)
    s.box(795,390,385,445,'AI-Thinker ESP32-CAM','#edf3ff')
    s.pin(985,390,'5V 輸入',V5)
    s.pin(795,550,'U0R / GPIO3（RX0）',TX,'left')
    s.pin(795,635,'U0T / GPIO1（TX0）',RX,'left')
    s.pin(1180,480,'GPIO0',GND)
    s.pin(985,835,'GND',GND)
    s.note(815,707,['按板上的 RESET 鍵重設', '燒錄完成移除 GPIO0 跳線', '再按 RESET 才會運行韌體'])
    s.wire([(535,205),(985,205),(985,390)],V5,4,True)
    s.text(650,239,'外部 5V 電源',18,V5)
    s.wire([(425,550),(795,550)],TX,3,True); s.text(505,532,'TX → RX',18,TX)
    s.wire([(795,635),(425,635)],RX,3,True); s.text(505,618,'RX ← TX',18,RX)
    s.wire([(1180,480),(1245,480),(1245,900)],GND,3,dash=True)
    s.text(1225,752,'燒錄',17,GND,anchor='end'); s.text(1225,783,'跳線',17,GND,anchor='end')
    s.wire([(50,900),(1245,900)],GND,4)
    s.wire([(65,265),(35,265),(35,900),(50,900)],GND,3)
    for x,y in [(245,810),(985,835)]: s.wire([(x,y),(x,900)],GND,3); s.dot(x,900,GND)
    s.dot(1245,900,GND)
    s.text(65,938,'共地：LM2596 OUT−／12V−、ESP32 GND、CP2102 GND；Mega 若接著也必須共地。',18)
    s.box(65,975,1115,150,'操作順序')
    s.note(85,1035,['關馬達分支 → GPIO0 接 GND → RESET → VS Code 上傳 → 移除跳線 → RESET → 串列輸入 ACCESS',
      'Mega 的 D18／D19 與 ESP32 GPIO13／14 可保留電平轉換接線；與 CP2102 使用不同 UART。',
      'CP2102 的 5V／3V3 不接 ESP32；USB 電源與 LM2596 輸出不並接。'],size=17)
    s.save('wiring-flashing.svg')


if __name__ == '__main__':
    runtime(); flashing()
    print('Generated esp32cam/wiring-runtime.svg and wiring-flashing.svg')
