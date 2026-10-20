#!/usr/bin/env python3
"""Static topology checks; does not claim runtime or electrical validation."""
import json
from pathlib import Path
p=Path(__file__).parent
d=json.loads((p/'diagram.json').read_text())
parts={x['id']:x for x in d['parts']}
edges={frozenset(x[:2]) for x in d['connections']}
def wired(a,b): assert frozenset((a,b)) in edges,(a,b)
assert len(parts)==len(d['parts'])
assert sum(x['type']=='chip-mcp23017' for x in parts.values())==4
assert sum(x['type']=='chip-unipolar' for x in parts.values())==10
assert sum(x['type']=='wokwi-slide-switch' for x in parts.values())==10
for m in range(4):
 for pin,gpio in [('SDA',21),('SCL',22),('RESET',23)]: wired(f'esp:{gpio}',f'm{m}:{pin}')
 for bit in range(3): wired('rails:'+('V5_LOGIC' if m&(1<<bit) else 'GND'),f'm{m}:A{bit}')
for u in range(10):
 for b in range(4):
  gp=(u%4)*4+b
  wired(f'm{u//4}:GP'+('A' if gp<8 else 'B')+str(gp%8),f'u{u}:IN{b+1}')
 bit=u if u<7 else u+1
 wired(f'h{u}:2','m3:GP'+('A' if bit<8 else 'B')+str(bit%8))
 wired(f'h{u}:1',f'u{u}:HALL')
 assert parts[f'h{u}']['attrs']['value']=='0'
 wired('rails:V12_MOTOR',f'u{u}:COMMON_12V')
for e in edges:
 assert not ('rails:V12_MOTOR' in e and any(x.startswith('esp:') for x in e))
print('PASS: four addressed MCPs, 40 phases, 10 Hall inputs, GPIO21/22/23, no 12V ESP32 wire')
