// SPDX-License-Identifier: GPL-3.0-only
// Actual firmware setup/loop/serial parser + register transport + ideal rotor plant.
// Hall is derived exclusively from output latch driven shaft position, never scripted.
#include <iostream>
#include <stdexcept>
#include <string>
#include "Arduino.h"
#include "Wire.h"
uint32_t fakeNow=0;
int fakeResetLevel=HIGH;
std::vector<int> fakeGpioEvents;
FakeSerial Serial;
FakeWire Wire;
void fakeResetExpanders(){Wire.resetChips();}
#include "../../src/main.cpp"
#define CHECK(e) do{if(!(e))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #e);}while(0)
struct Rotor {int pos=0,phase=-1,mask=0;bool disconnected=false,stuckLow=false;};
Rotor rotors[10];
void plant(){
  const int seq[8]={1,3,2,6,4,12,8,9};Wire.hallRaw=board::kHallPinMask;
  for(unsigned u=0;u<10;u++){
    auto& r=rotors[u];unsigned c=u/4,shift=(u%4)*4;
    unsigned latch=Wire.registers[c][0x14]|unsigned(Wire.registers[c][0x15])<<8;
    int mask=fakeResetLevel==LOW?0:(latch>>shift)&15;
    if(mask!=r.mask && mask){
      int p=-1;for(int i=0;i<8;i++)if(seq[i]==mask)p=i;
      CHECK(p>=0);
      if(r.phase>=0){int d=(p-r.phase+8)%8;
        if(d==1)r.pos=(r.pos+1)%4096;
        if(d==7)r.pos=(r.pos+4095)%4096;
      }
      r.phase=p;
    }
    r.mask=mask;
    if((r.pos<32 && !r.disconnected)||r.stuckLow){unsigned bit=u<7?u:u+1;Wire.hallRaw&=~(1u<<bit);}
  }
}
void tick(){plant();fakeNow+=1000;loop();}
void command(const std::string& s){Serial.feed(s+"\n");for(unsigned i=0;i<20 && Serial.available();i++)tick();CHECK(!Serial.available());}
void waitIdle(){for(unsigned i=0;i<200000 && motion.busy();i++)tick();CHECK(!motion.busy());}
void boot(){
 motion=MotionController();settings=Preferences();Serial=FakeSerial();Wire=FakeWire();
 fakeNow=0;fakeResetLevel=HIGH;settingsAvailable=initialized=armed=noUmlauts=false;
 enabledCount=0;hallMask=activeHighMask=0;lastHallUs=lastAuditUs=lastLoopUs=0;
 commandLength=0;discardLine=false;
 for(int i=0;i<10;i++){rotors[i]=Rotor();rotors[i].pos=i*359;}
 setup();plant();
}
void initialize(int count){command("INIT POWER_OFF");CHECK(initialized);command("ENABLE "+std::to_string(count));command("LIMIT 10");command("ARM");CHECK(armed);}
void allReady(int n){for(int i=0;i<n;i++){CHECK(motion.homed(i));CHECK(motion.state(i)==State::Ready);CHECK(motion.coilMask(i)==0);}}
int main(){try{
 boot();for(int i=0;i<100;i++)tick();CHECK(!motion.busy());CHECK(!initialized);CHECK(!armed);
 initialize(10);command("HOME ALL");waitIdle();CHECK(initialized);allReady(10);
 command("TEXT HELLO");waitIdle();allReady(10);CHECK(motion.currentIndex(0)==8);CHECK(motion.currentIndex(4)==15);CHECK(motion.currentIndex(9)==0);
 // Every flap, including forward wrap/rehome, using actual command parsing.
 for(int slot=0;slot<45;slot++){command("MOVE 0 "+std::to_string(slot));waitIdle();allReady(10);CHECK(motion.currentIndex(0)==slot);}
 command("MOVE 0 1");waitIdle();allReady(10);CHECK(motion.currentIndex(0)==1);
 command("CAL 0 4096 45 -1 3000");CHECK(!armed);command("ARM");command("HOME 0");waitIdle();allReady(10);
 command("SAVE");auto saved=settings.bytes;CHECK(!saved.empty());
 command("MOVE 0 44");tick();command("STOP");CHECK(!armed);CHECK(!motion.homed(0));CHECK(!motion.busy());plant();for(auto& r:rotors)CHECK(r.mask==0);
 boot();settings.bytes=saved;loadSettings();CHECK(motion.config(0).homeOffsetSteps==45);CHECK(motion.config(0).phaseDirection==-1);CHECK(!armed);CHECK(enabledCount==0);
 boot();initialize(1);rotors[0].disconnected=true;command("HOME ALL");waitIdle();CHECK(!initialized);CHECK(!armed);CHECK(fakeResetLevel==LOW);CHECK(Serial.output.find("HOME_STEP_LIMIT")!=std::string::npos || Serial.output.find("home-step-limit")!=std::string::npos || Serial.output.find("MOTION_HOME")!=std::string::npos);
 boot();initialize(1);rotors[0].stuckLow=true;command("HOME ALL");waitIdle();CHECK(!initialized);CHECK(fakeResetLevel==LOW);
 boot();initialize(10);command("HOME ALL");for(int i=0;i<100;i++)tick();Wire.failAll=true;tick();tick();tick();CHECK(!initialized);CHECK(fakeResetLevel==LOW);
 std::cout<<"PASS closed loop: production setup/loop/parser, ten automatic Hall homes, TEXT, all 45 slots, wrap/rehome, reverse/offset calibration, NVS reload, STOP, Hall disconnected/stuck, I2C loss\n";
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
