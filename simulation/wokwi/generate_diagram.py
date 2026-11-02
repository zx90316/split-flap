#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Rebuild the complete digital-only Wokwi diagram deterministically."""
import json
from pathlib import Path
parts=[];connections=[]
def part(t,i,x,y,attrs=None):
    parts.append(dict(type=t,id=i,left=x,top=y,attrs=attrs or {}))
def wire(a,b,color='green',route=None):connections.append([a,b,color,route or []])
def label(i,x,y,text):part('wokwi-text',i,x,y,{'text':text})
label('title',-300,-330,'TEN-CHARACTER SPLIT-FLAP: DIGITAL LOGIC SIMULATION')
label('limits',-300,-290,'Custom MCP23017 + ideal ULN2003/unipolar phase observer.\nAutomatic position-driven Hall; no 12V electrical, current, torque or real magnet physics.\n3.3V/5V level shifting is omitted in this Boolean model; see docs/esp32/wiring.md for REAL wiring.')
part('board-esp32-devkit-c-v4','esp',-300,-140)
part('chip-rails','rails',-290,230)
label('power_note',-300,340,'DIGITAL rail labels only; outputs are Boolean HIGH.\n12V_MOTOR never connects to ESP32.')
wire('esp:TX',' $serialMonitor:RX'.strip(),'');wire('esp:RX','$serialMonitor:TX','')
wire('rails:GND','esp:GND.1','black')
for m in range(4):
    x=60+m*360
    part('chip-mcp23017',f'm{m}',x,-100)
    label(f'mlabel{m}',x,-165,f'{"H0" if m==3 else "M"+str(m)} | 0x{32+m:02X}\n'+('Hall GPA0-6, GPB0-2' if m==3 else ('U8-U9' if m==2 else f'U{m*4}-U{m*4+3}')))
    for pin,gpio,col in [('SDA','21','green'),('SCL','22','blue'),('RESET','23','orange')]:
        wire(f'esp:{gpio}',f'm{m}:{pin}',col)
    wire('rails:V5_LOGIC',f'm{m}:VDD','red');wire('rails:GND',f'm{m}:VSS','black')
    for bit in range(3):wire(f'rails:{"V5_LOGIC" if m&(1<<bit) else "GND"}',f'm{m}:A{bit}','red' if m&(1<<bit) else 'black')
for u in range(10):
    # Two rows of five modules; all forty phase connections are explicit.
    x=(u%5)*320-240;y=530+(u//5)*340
    part('chip-unipolar',f'u{u}',x,y,{'unit':str(u),'position':str(u*359),'revolution':'4096','hallWidth':'32'})
    label(f'ulabel{u}',x,y-50,f'U{u} | ULN2003 + 12V 28BYJ-48\n4-coil rotor + automatic Hall')
    m=u//4;off=(u%4)*4
    for b,col in enumerate(['green','blue','purple','orange']):
        gp=off+b;wire(f'm{m}:GP{"A" if gp<8 else "B"}{gp%8}',f'u{u}:IN{b+1}',col)
    wire('rails:V12_MOTOR',f'u{u}:COMMON_12V','red');wire('rails:GND',f'u{u}:GND','black')
    part('wokwi-slide-switch',f'h{u}',x+180,y+95,{'value':'0','bounce':'0'})
    label(f'hlabel{u}',x+160,y+150,f'H_U{u} Hall fault switch\nleft=AUTO, right=disconnected')
    bit=u if u<7 else u+1
    wire(f'h{u}:2',f'm3:GP{"A" if bit<8 else "B"}{bit%8}','cyan')
    wire(f'h{u}:1',f'u{u}:HALL','cyan')
label('legend',-240,1250,'Motor display: four green coil indicators, estimated shaft pointer (assumed 4096 half-steps/rev),\ngreen status bar=legal phase order; red=illegal/skipped phase. Hall follows rotor; switches disconnect Hall for fault injection.\nProduction serial: INIT POWER_OFF | ENABLE 10 | LIMIT 10 | ARM | HOME ALL | STATUS | TEXT HELLO | STOP')
diagram=dict(version=1,author='Split-flap project',editor='wokwi',parts=parts,connections=connections,serialMonitor={'display':'always','newline':'lf'})
Path(__file__).with_name('diagram.json').write_text(json.dumps(diagram,indent=2)+'\n')
