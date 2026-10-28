// SPDX-License-Identifier: GPL-3.0-only
#include "mcp_bus.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
struct FakeBus {
  uint8_t registers[4][32] = {};
  bool good = true;
  std::vector<int> writes;
  bool read8(uint8_t a,uint8_t r,uint8_t& v) { if(!good)return false; v=registers[a-0x20][r];return true; }
  bool read16(uint8_t a,uint8_t r,uint16_t& v) { if(!good)return false;v=registers[a-0x20][r]|(registers[a-0x20][r+1]<<8);return true; }
  bool write16(uint8_t a,uint8_t r,uint16_t v) { if(!good)return false;writes.push_back((a<<8)|r);registers[a-0x20][r]=v;registers[a-0x20][r+1]=v>>8;return true; }
};
int main() {
  FakeBus io; McpBus<FakeBus> m(io); assert(m.begin());
  assert(io.writes.size()==12);
  for(unsigned i=0;i<4;i++){assert((io.writes[3*i]&255)==0x14);assert((io.writes[3*i+2]&255)==0);}
  assert(io.registers[3][0]==0x7f && io.registers[3][1]==0x07);
  uint8_t coils[10]={1,2,3,4,5,6,7,8,9,10};io.writes.clear();assert(m.flush(coils));assert(io.writes.size()==3);
  uint16_t v=0;io.read16(0x20,0x14,v);assert(v==0x4321);io.read16(0x21,0x14,v);assert(v==0x8765);io.read16(0x22,0x14,v);assert(v==0x00a9);
  io.writes.clear();assert(m.flush(coils));assert(io.writes.empty());
  io.write16(0x23,0x12,0x077f);assert(m.readHalls(v));assert(v==0);
  for(int unit=0;unit<10;unit++){uint16_t pin=uint16_t(1)<<(unit<7?unit:unit+1);io.write16(0x23,0x12,0x077f&~pin);assert(m.readHalls(v));assert(v==(1<<unit));}
  io.write16(0x23,0x12,0x077f);assert(m.readHalls(v,0x201));assert(v==0x201);
  assert(m.audit());io.registers[1][0]=0xff;assert(!m.audit());assert(!m.flush(coils));
  FakeBus io2;McpBus<FakeBus> m2(io2);assert(m2.begin());io2.good=false;assert(!m2.readHalls(v));assert(!m2.healthy());
  FakeBus io3;io3.registers[0][0x0a]=0x20;McpBus<FakeBus> m3(io3);assert(!m3.begin());
  FakeBus io4;McpBus<FakeBus> m4(io4);assert(m4.begin());io4.good=false;coils[0]=2;assert(!m4.flush(coils));assert(!m4.healthy());
  std::cout<<"MCP mapping, atomic port batching, startup order, active polarity, audit and error tests passed\n";
}
