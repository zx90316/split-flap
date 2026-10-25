#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate four single-controller wiring SVGs. Coordinates are functional, not pin pitch."""
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
    s=Svg(1440,1080,'單主控正常運作：ESP32-CAM＋四片 MCP23017','四片使用 0x20～0x23；六片庫存中兩片備用。依板上印字接線，圖框不是實際排針順序。')
    s.box(45,140,290,150,'12V 電源')
    s.note(65,210,['正端經保險絲／主開關','負端回共地'])
    s.box(440,140,290,150,'LM2596：5.0V')
    s.note(460,210,['IN+ 接 12V','IN−／OUT− 接共地'])
    s.box(1050,140,335,150,'馬達 12V 分支')
    s.note(1070,210,['獨立實體開關','供各 ULN 正端／COM'])
    s.wire([(335,180),(400,180),(440,180)],V12,4,True)
    s.wire([(400,180),(400,125),(1010,125),(1010,180),(1050,180)],V12,4,True);s.dot(400,180,V12)
    s.wire([(730,180),(950,180),(950,335),(210,335),(210,390)],V5,4,True)
    s.wire([(950,335),(1220,335),(1220,390)],V5,4,True);s.dot(950,335,V5)
    s.text(490,324,'控制 5V：ESP32 5V、四片 MCP VCC、Hall VCC、P011 VCCB',18,V5)
    s.box(45,390,350,370,'AI-Thinker ESP32-CAM','#edf3ff')
    s.pin(395,480,'GPIO13 / SDA',TX);s.pin(395,550,'GPIO14 / SCL',RX);s.pin(395,620,'GPIO15 / OD RST',V12)
    s.pin(395,700,'3V3 輸出',V3);s.pin(210,390,'5V',V5,'left');s.pin(210,760,'GND',GND)
    s.text(65,448,'相機／SD／閃光燈停用',17)
    s.box(560,390,320,370,'P011 現有 8 路板','#f5effa')
    for y,a,b,c in [(480,'A0','B0',TX),(550,'A1','B1',RX),(620,'A2','B2',V12)]:
        s.pin(560,y,a,c,'left');s.pin(880,y,b,c)
        s.wire([(395,y),(560,y)],c,3)
        s.wire([(660,y),(780,y)],c,2)
    s.pin(560,700,'VCCA',V3,'left');s.pin(880,700,'VCCB',V5);s.pin(720,760,'GND',GND)
    s.wire([(395,700),(560,700)],V3,3)
    s.wire([(950,335),(950,700),(880,700)],V5,3)
    s.text(580,738,'A3～A7／B3～B7 不接；無 OE',16)
    s.box(1060,390,325,370,'四片 MCP23017','#e7f3ec')
    s.pin(1220,390,'VCC 5V',V5,'left')
    for y,label,c in [(480,'全部 SDA',TX),(550,'全部 SCL',RX),(620,'全部 RST',V12)]:
        s.pin(1060,y,label,c,'left');s.wire([(880,y),(1060,y)],c,3)
    s.text(1080,680,'0x20／0x21／0x22 → ULN',17)
    s.text(1080,714,'0x23 ← 十路 Hall',17)
    s.pin(1220,760,'全部 GND',GND)
    s.wire([(25,795),(1385,795)],GND,4)
    for x in [210,720,1220]:s.wire([(x,760),(x,795)],GND,3);s.dot(x,795,GND)
    s.wire([(65,290),(25,290),(25,795)],GND,3)
    s.pin(65,290,'GND',GND,'left');s.pin(585,290,'GND',GND,'left')
    s.wire([(585,290),(585,307),(420,307),(420,795)],GND,3);s.dot(420,795,GND)
    s.box(1060,855,325,155,'10 組 ULN＋12V 馬達')
    s.note(1080,920,['相位接法見 U0～U9 圖','馬達五線插頭接原 ULN 插座'],size=16)
    s.wire([(1385,245),(1420,245),(1420,910),(1385,910)],V12,4,True)
    s.pin(1220,1010,'ULN GND',GND)
    s.wire([(1220,1010),(1220,1028),(1430,1028),(1430,795),(1385,795)],GND,3)
    s.wire([(1385,720),(1400,720),(1400,835),(1210,835),(1210,855)],TX,3,True)
    s.box(45,840,945,175,'接線要點')
    s.note(65,900,['P011 只轉換訊號；MCP VCC 接控制 5V，不接 12V。所有 GND 共地。',
      '移除原 ESP32 GPIO13／14 到 Mega 的 UART 線；不插 SD 卡。',
      '各板都有電才接訊號；先關馬達分支再改線。RST 拉低不等於切斷 12V。'],size=17)
    s.text(45,1050,'線色：紅=5V　紫=3.3V　橘=12V／重設訊號（依標籤區分）　藍=SDA／相位　綠=SCL　灰黑=共地。',17)
    s.save('wiring-runtime.svg')

def flashing():
    s=Svg(1300,920,'單主控燒錄：一條 CP2102 USB','CP2102 只接 TXD、RXD、GND；控制電源由 LM2596 的穩壓 5.0V 提供。')
    s.box(60,150,500,130,'12V → LM2596 → 5.0V')
    s.note(80,215,['馬達 12V 分支關閉；控制電源保留','外部 5V 接 ESP32 板上「5V」腳'])
    s.box(60,340,365,310,'電腦 USB → CP2102','#e7f3ec')
    s.pin(425,420,'TXD（3.3V 邏輯）',TX);s.pin(425,500,'RXD',RX);s.pin(240,650,'GND',GND)
    s.text(80,568,'5V／3V3／VCC：全部不接',18,V5)
    s.text(80,610,'DTR／RTS：不接',18)
    s.box(835,300,385,350,'AI-Thinker ESP32-CAM','#edf3ff')
    s.pin(1025,300,'5V',V5,'left');s.pin(835,420,'U0R / GPIO3',TX,'left');s.pin(835,500,'U0T / GPIO1',RX,'left')
    s.pin(1220,365,'GPIO0',GND);s.pin(1025,650,'GND',GND)
    s.text(855,568,'板上 RESET：按下重設',18)
    s.text(855,610,'GPIO13／14／15 為 MCP 訊號',17)
    s.wire([(560,200),(1025,200),(1025,300)],V5,4,True)
    s.wire([(425,420),(835,420)],TX,3,True);s.text(530,402,'TX → RX',18,TX)
    s.wire([(835,500),(425,500)],RX,3,True);s.text(530,482,'RX ← TX',18,RX)
    s.wire([(40,700),(1265,700)],GND,4)
    for x in [240,1025]:s.wire([(x,650),(x,700)],GND,3);s.dot(x,700,GND)
    s.wire([(60,245),(40,245),(40,700)],GND,3)
    s.wire([(1220,365),(1265,365),(1265,700)],GND,3,dash=True)
    s.box(60,750,1160,135,'燒錄順序')
    s.note(80,810,['關馬達支路 → GPIO0 接 GND → 按 RESET → VS Code 上傳 → 移除 GPIO0 跳線 → RESET',
      '先備份舊網頁設定；單主控首次用 USB 燒錄。之後網頁只上傳本版 .sffw；MCP 不需燒錄。'],size=17)
    s.save('wiring-flashing.svg')

def motors():
    s=Svg(1440,1170,'MCP → ULN2003：U0～U9 四相逐線表','每列四個 MCP 腳依序接同一片 ULN 的 IN1、IN2、IN3、IN4；馬達插頭維持接 ULN 原插座。')
    for x,label in [(60,'MCP 地址'),(245,'顯示位'),(410,'MCP 腳位（依序）'),(820,'ULN 輸入（依序）'),(1190,'馬達')]:s.text(x,165,label,20,weight='bold')
    for u in range(10):
        y=220+u*62;chip=u//4;bank='A' if u%4<2 else 'B';first=(u%2)*4
        s.text(60,y,f'0x{0x20+chip:02X}',21)
        s.text(245,y,f'U{u}',22,weight='bold')
        s.text(410,y,' / '.join(f'GP{bank}{first+n}' for n in range(4)),19,TX)
        s.text(820,y,'IN1 / IN2 / IN3 / IN4',19,RX)
        s.text(1190,y,f'U{u} 五線插頭',18)
        s.wire([(45,y+24),(1385,y+24)],'#d4dfd8',1)
    s.box(45,855,655,245,'電源與地址')
    s.note(65,915,['MCP VCC＝控制 5V；GND＝共地。',
      '地址 A2 A1 A0：0x20＝000，0x21＝001，0x22＝010。',
      '每片 ULN 正端／COM＝有開關的 12V_MOTOR。',
      'ULN GND 回電源共地；不能把 COM 接 5V。',
      '未啟用 U 的相位輸出低；0x22 GPB 全部保持低。'],size=17)
    s.box(730,855,655,245,'相序與模組名稱')
    s.note(750,915,['以實際 IN1～IN4 印字接線；若需調相序用 ORDER。',
      '四路是驅動訊號，不直接供應線圈電流。',
      'MCP 地址 A0/A1/A2、GPA/GPB 和 P011 A0 互不相同。',
      '四片 MCP 的 SDA／SCL／RST 並接到正常運作圖。',
      '只使用 0x20～0x22 作馬達，0x23 保留給 Hall。'],size=17)
    s.text(45,1140,'相位對應與原版 45 片順序維持；每圈步數與方向依各機構校準。',18)
    s.save('wiring-motors.svg')

def halls():
    s=Svg(1300,1000,'十路 Hall → MCP 0x23','預設關閉 Hall；接好與安裝磁鐵後，在設定頁啟用。GPA7／GPB7 不接 Hall。')
    for x,label in [(65,'感測位置'),(320,'KY-003 訊號'),(655,'MCP 0x23'),(970,'各感測板供電')]:s.text(x,165,label,21,weight='bold')
    for u in range(10):
        y=220+u*48;pin=f'GPA{u}' if u<7 else f'GPB{u-7}'
        s.text(65,y,f'U{u}',21,weight='bold');s.text(320,y,'S／OUT →',20,TX);s.text(655,y,pin,21,RX)
        s.text(970,y,'VCC 5V／GND 共地',18)
        s.wire([(45,y+18),(1240,y+18)],'#d4dfd8',1)
    s.box(45,745,1195,200,'地址、重設與測試')
    s.note(65,805,['0x23 地址 A2 A1 A0＝011。VCC＝5V，GND＝共地，RST 接四片共用重設線。',
      'GPA7／GPB7 與其餘未使用輸出保持低；每路 S／OUT 分開，不把十路訊號短接。',
      '核對 KY-003 板上 S／+／−；不要照模組外觀猜針序。預設 POLARITY LOW，依實測確認。',
      '先 WATCH 看原始訊號，再試 HOME／MEASURE；尚無磁鐵時沿用手動定位。'],size=18)
    s.save('wiring-hall.svg')

if __name__=='__main__':
    runtime();flashing();motors();halls()
    print('Generated four single-controller wiring SVGs')
