#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate reviewable SVG wiring from the firmware pin table. No dependencies."""
from html import escape
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent
source = (ROOT / 'unit_test/pins.h').read_text(encoding='utf-8')
PINS = [list(map(int, row.split(','))) for row in re.findall(r'\{(\d+(?:,\s*\d+){3})\}', source)]
assert len(PINS) == 10
RED, GREEN, BLACK, BLUE = '#dc3545', '#07836d', '#334155', '#2563eb'
PHASES = ['#d97706', '#8b5cf6', '#2563eb', '#c026a3']


class Svg:
    def __init__(self, width, height, title, desc):
        self.items = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
                      f'<title id="title">{escape(title)}</title><desc id="desc">{escape(desc)}</desc>',
                      '<style>text{font-family:"Microsoft JhengHei","Noto Sans CJK TC",sans-serif;fill:#172b45} .wire{fill:none;stroke-linejoin:round;stroke-linecap:round}</style>',
                      f'<rect width="{width}" height="{height}" fill="#f5f7fb"/>']
        self.text(45, 55, title, 30, weight='bold')
        self.text(45, 88, desc, 17)

    def text(self, x, y, value, size=18, color=None, weight='normal', anchor='start'):
        fill = f' style="fill:{color}"' if color else ''
        self.items.append(f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{weight}" text-anchor="{anchor}"{fill}>{escape(str(value))}</text>')

    def box(self, x, y, w, h, fill='white', stroke='#cbd5e1', radius=12):
        self.items.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')

    def wire(self, points, color=BLACK, width=3, dashed=False):
        dash = ' stroke-dasharray="7 5"' if dashed else ''
        data = ' '.join(f'{x},{y}' for x, y in points)
        self.items.append(f'<polyline class="wire" points="{data}" stroke="{color}" stroke-width="{width}"{dash}/>')

    def dot(self, x, y, color=BLACK, radius=5):
        self.items.append(f'<circle cx="{x}" cy="{y}" r="{radius}" fill="{color}"/>')

    def save(self, name):
        (ROOT / name).write_text('\n'.join(self.items + ['</svg>', '']), encoding='utf-8')


def single():
    s = Svg(1280, 930, '第一階段：只接 U0，一顆一顆確認', 'Mega 2560 用 USB 供電與收命令；12V 只供馬達。示意連線，非模組實際針序或等比例外觀。')
    s.box(45, 110, 1190, 49, '#fff1f2', '#fecdd3')
    s.text(65, 141, '12V 不接 Mega、Hall 或 GPIO；VIN／DC 圓孔不接電。先關馬達分支，再拔 USB。', 20, '#b42334')
    s.box(65, 210, 330, 440, '#eaf2ff', '#93b4f5')
    s.text(88, 250, 'Arduino Mega 2560', 25, weight='bold')
    s.text(88, 282, '預設 U0；其他相位全部 LOW', 17)
    s.box(95, 176, 225, 40, '#dbeafe', '#93b4f5')
    s.text(112, 202, 'USB ← 電腦（5V＋資料）', 16)
    s.box(760, 230, 260, 280, '#fff7ed', '#edc28b')
    s.text(785, 262, 'ULN2003 · U0', 23, weight='bold')
    for i, y in enumerate([310, 350, 390, 430]):
        s.text(295, y + 6, f'D{PINS[0][i]}', 21)
        s.text(778, y + 6, f'IN{i+1}', 20)
        s.wire([(395, y), (760, y)], PHASES[i], 4)
        s.dot(395, y, PHASES[i]); s.dot(760, y, PHASES[i])
    s.text(505, 291, '4P：Mega 端公 → ULN 端母', 17)
    s.text(88, 335, '同一排：22 / 24 / 26 / 28', 15)
    s.text(88, 365, '不是 22 / 23 / 24 / 25', 15)
    s.text(88, 405, '只驅動 ULN 的輸入', 16)
    s.text(88, 434, '不直接供電給馬達線圈', 16)
    s.box(1080, 300, 155, 165, '#fff7ed', '#edc28b')
    s.text(1098, 338, '28BYJ-48', 20, weight='bold')
    s.text(1098, 370, '12V · 五線', 18)
    s.text(1098, 405, 'U0 滾筒', 18)
    s.wire([(1020, 355), (1080, 355)], '#d97706', 6)
    s.text(1028, 290, '原五線插頭', 15)
    s.text(786, 493, '+ / COM', 17, RED)
    s.text(938, 493, 'GND', 17)
    s.dot(820, 510, RED); s.dot(970, 510)
    s.box(760, 560, 260, 160, '#e8f7f1', '#86cdb8')
    s.text(815, 593, 'KY-003 · U0', 23, weight='bold')
    for label, y, c in [('S', 620, GREEN), ('+', 660, RED), ('−', 700, BLACK)]:
        s.text(782, y + 5, label, 21, c); s.dot(760, y, c)
    s.text(850, 635, '3P：S / + / −', 16)
    s.text(850, 666, '腳序依實物調整', 16)
    s.text(850, 697, '可經 Hall 分線座', 16)
    s.text(335, 506, 'A0', 21, GREEN)
    s.text(333, 566, '5V', 21, RED)
    s.text(323, 626, 'GND', 20)
    s.wire([(395, 500), (625, 500), (625, 620), (760, 620)], GREEN, 4)
    s.wire([(395, 560), (580, 560), (580, 660), (760, 660)], RED, 3)
    s.wire([(395, 620), (435, 620), (435, 810)], BLACK, 4)
    s.wire([(760, 700), (735, 700), (735, 810)], BLACK, 3)
    s.box(65, 746, 210, 94)
    s.text(82, 777, '隔離式 12V 電源', 20, weight='bold')
    s.text(82, 811, '+12V', 16, RED)
    s.text(210, 811, '0V / −', 16)
    for x, w, label in [(320, 90, '保險絲'), (450, 100, '主開關'), (590, 130, '馬達切斷')]:
        s.box(x, 746, w, 44, '#fff1f2', '#fecdd3')
        s.text(x + w / 2, 774, label, 17, anchor='middle')
    s.wire([(275, 765), (320, 765)], RED, 5)
    s.wire([(410, 765), (450, 765)], RED, 5)
    s.wire([(550, 765), (590, 765)], RED, 5)
    s.wire([(720, 765), (1050, 765), (1050, 540), (820, 540), (820, 510)], RED, 5)
    s.wire([(275, 810), (1070, 810), (1070, 525), (970, 525), (970, 510)], BLACK, 5)
    for x in [435, 735]: s.dot(x, 810)
    s.text(330, 838, '共地分配點；馬達回流獨立走線。交叉無圓點不相接。', 17)
    s.text(65, 880, '測試順序：WATCH → 拔馬達做 LED → STEP → HOME → MEASURE → OFFSET → FLAP / GOTO / CYCLE', 18)
    s.text(65, 908, '每個動作前須 ARM；運動中任意非換行輸入停止整行。LED 不等於相序或硬體安全驗證。', 17)
    s.save('wiring-single.svg')


def ten():
    s = Svg(1440, 1190, '十字元接線規劃：40 路相位＋10 路 Hall', '支援 1～10 位同時翻轉；只啟用已接組別。擴充前量控制 5V 負載、12V 總電流與壓降。')
    s.text(55, 134, 'Mega 2560（標準母排）', 23, weight='bold')
    s.text(580, 134, 'ULN2003 ×10', 23, weight='bold')
    s.text(865, 134, '12V 馬達 ×10', 23, weight='bold')
    s.text(1155, 134, 'KY-003 ×10', 23, weight='bold')
    s.box(45, 170, 370, 784, '#eaf2ff', '#93b4f5')
    for u, pins in enumerate(PINS):
        y = 188 + u * 76
        s.text(65, y + 25, f'U{u}: ' + ' / '.join('D' + str(p) for p in pins), 18, weight='bold')
        s.text(65, y + 53, f'Hall S{u} → A{u}      相位線為 4P 公對母', 15, GREEN)
        s.box(590, y, 205, 56, '#fff7ed', '#edc28b')
        s.text(608, y + 23, f'U{u} · IN1 / 2 / 3 / 4', 16)
        s.text(610, y + 47, '+12V', 15, RED)
        s.text(735, y + 47, 'GND', 15)
        s.wire([(415, y + 23), (590, y + 23)], PHASES[u % 4], 5)
        s.text(460, y + 15, '4P', 16)
        s.box(865, y, 180, 56, '#fff7ed', '#edc28b')
        s.text(884, y + 24, f'U{u}  28BYJ-48', 17)
        s.text(884, y + 47, '五線插頭 · 12V', 15)
        s.wire([(795, y + 23), (865, y + 23)], '#d97706', 4)
        s.box(1155, y, 220, 56, '#e8f7f1', '#86cdb8')
        s.text(1172, y + 23, f'U{u}: S{u} / + / −', 18)
        s.text(1172, y + 47, '3P 母對母 → 分線板', 15)
        s.wire([(1375, y + 12), (1395, y + 12)], RED, 2)
        s.dot(1395, y + 12, RED, 3)
        s.wire([(1375, y + 44), (1415, y + 44)], BLACK, 2)
        s.dot(1415, y + 44, BLACK, 3)
        s.wire([(1250, y + 56), (1250, y + 66), (435, y + 66), (435, y + 50), (415, y + 50)], GREEN, 2)
        s.wire([(555, y + 47), (590, y + 47)], RED, 3)
        s.dot(555, y + 47, RED, 4)
        s.wire([(750, y + 56), (750, y + 61), (825, y + 61)], BLACK, 2)
        s.dot(825, y + 61, BLACK, 4)
    s.wire([(555, 235), (555, 1010)], RED, 4)
    s.wire([(825, 249), (825, 1060)], BLACK, 4)
    s.wire([(415, 182), (1395, 182), (1395, 884)], RED, 3)
    s.text(326, 193, '5V', 15, RED)
    s.text(1285, 173, 'Hall +5V', 14, RED)
    s.wire([(1415, 232), (1415, 1060), (825, 1060)], BLACK, 3)
    s.wire([(415, 946), (475, 946), (475, 1060)], BLACK, 3)
    s.text(325, 949, 'GND', 14)
    s.dot(475, 1060, BLACK, 4)
    s.box(45, 985, 370, 106)
    s.text(65, 1017, '12V → 保險絲 → 主開關', 20)
    s.text(65, 1049, '→ 馬達分支開關 → 分配端子', 20)
    s.text(65, 1078, '各片正負回到端子台（星狀分配）', 16)
    s.wire([(415, 1010), (555, 1010)], RED, 5)
    s.wire([(415, 1060), (825, 1060)], BLACK, 5)
    s.box(875, 985, 500, 106, '#e8f7f1', '#86cdb8')
    s.text(895, 1017, 'Hall 分線板：+ 全接 Mega 5V，− 全接 GND', 19)
    s.text(895, 1049, '8P：S0–S7 → A0–A7；2P：S8–S9 → A8–A9', 17)
    s.text(895, 1078, '控制 5V／USB 供電互斥；正負與訊號分開標記', 17)
    s.text(45, 1135, '紅：12V 馬達分配；深灰：馬達地；綠：Hall 信號（經分線板）。交叉無圓點不代表相接。', 19)
    s.text(45, 1167, '電腦調機：Mega 接 USB、外部 5V 拔除；獨立供電依 ESP32 圖。全部共地；U9 使用 D8–D11。', 18)
    s.save('wiring-ten.svg')


def hall_board():
    s = Svg(1280, 880, 'Hall 分線板：元件面逐孔配置（獨立焊盤）', '5×7cm 的初始配置；孔列 A–Z、孔行 1–17。圖中自訂 S / + / −，焊前必須按 KY-003 實物重排。')
    x0, y0, pitch = 90, 190, 30
    s.box(55, 145, 830, 570, '#eef5e8', '#b7caa5')
    for col in range(26):
        s.text(x0 + col * pitch, 178, chr(65 + col), 13, anchor='middle')
        for row in range(17):
            s.dot(x0 + col * pitch, y0 + row * pitch, '#c0c9b9', 3)
    for row in range(17): s.text(69, y0 + row * pitch + 5, row + 1, 13)
    for u in range(10):
        x = x0 + (1 + u * 2) * pitch
        s.text(x, 132, f'U{u}', 17, anchor='middle', weight='bold')
        for row, color, label in [(2, GREEN, 'S'), (3, RED, '+'), (4, BLACK, '−')]:
            s.dot(x, y0 + row * pitch, color, 8)
        output_col = 1 + u if u < 8 else 10 + u - 8
        ox = x0 + output_col * pitch
        s.wire([(x, y0 + 2 * pitch), (x + 12, y0 + 2 * pitch), (x + 12, 345 + u * 7), (ox, 430), (ox, 460)], GREEN, 2)
        s.dot(ox, 460, GREEN, 8)
        s.text(ox, 489, f'S{u}', 14, GREEN, anchor='middle')
    # Dashed buses are insulated/soldered on the reverse side, not stripboard.
    s.wire([(120, 280), (660, 280)], RED, 5, dashed=True)
    s.wire([(120, 310), (660, 310)], BLACK, 5, dashed=True)
    s.text(160, 528, '8P：B10–I10 → A0–A7', 17, GREEN)
    s.text(160, 559, '2P：K10–L10 → A8–A9', 17, GREEN)
    s.wire([(720, 580), (795, 580), (795, 280), (660, 280)], RED, 3, dashed=True)
    s.wire([(750, 580), (825, 580), (825, 310), (660, 310)], BLACK, 3, dashed=True)
    s.dot(720, 580, RED, 8); s.dot(750, 580, BLACK, 8)
    s.text(680, 617, 'V14 +5V', 16, RED)
    s.text(680, 646, 'W14 GND', 16)
    s.box(925, 165, 310, 510)
    legend = [
        ('每組 3P 公針', BLACK), ('第 3 行：S0–S9 各自獨立', GREEN),
        ('第 4 行：全部 +5V', RED), ('第 5 行：全部 GND', BLACK),
        ('綠實線：正面絕緣跨線', GREEN), ('紅／灰虛線：背面母線', BLACK),
        ('綠線只焊 S 和輸出端', BLACK), ('其餘跨孔保留絕緣', BLACK),
        ('3P 線：兩端元件均公針', BLACK), ('才使用母對母', BLACK),
        ('8P／2P 線接 Mega 端', BLACK), ('須用公頭插入母排', BLACK),
        ('只測 U0：先焊 U0 即可', BLACK), ('不必一次焊完十組', BLACK)]
    for i, (text, color) in enumerate(legend): s.text(944, 199 + i * 33, text, 16, color)
    s.text(55, 758, '背面看板時左右會鏡像：以孔號及導通量測為準，不能把這張元件面直接當背面焊線圖。', 20)
    s.text(55, 795, '母線須自行焊接；各 S 不短接。用絕緣跨線跨越 5V／GND，不能以裸線相交。', 19)
    s.text(55, 833, '本圖不包含 12V 馬達電源。完成後斷電量正負不短路、十路 S 不互通，再接 Mega。', 19)
    s.save('hall-board.svg')


if __name__ == '__main__':
    single(); ten(); hall_board()
    print('Generated wiring-single.svg, wiring-ten.svg, hall-board.svg from pins.h')
