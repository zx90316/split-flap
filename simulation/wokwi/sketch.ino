// SPDX-License-Identifier: GPL-3.0-only
// SIMULATION DIAGNOSTIC ONLY. Not the hardware motion-control firmware.
#include <Arduino.h>
#include <Wire.h>
constexpr uint8_t RESET_PIN=23;
constexpr uint8_t PHASES[8]={1,3,2,6,4,12,8,9};
constexpr uint8_t HALL_BITS[10]={0,1,2,3,4,5,6,8,9,10};
uint16_t outputs[3]={0,0,0};
bool ready=false,running=true;
int selected=-1;
uint8_t phase=0;
uint32_t lastStep=0,lastHall=0;
uint16_t oldHall=0xffff;

bool write16(uint8_t address,uint8_t reg,uint16_t value) {
  Wire.beginTransmission(address);Wire.write(reg);Wire.write(uint8_t(value));Wire.write(uint8_t(value>>8));
  return Wire.endTransmission()==0;
}
bool read16(uint8_t address,uint8_t reg,uint16_t &value) {
  Wire.beginTransmission(address);Wire.write(reg);
  if(Wire.endTransmission(false)!=0)return false;
  if(Wire.requestFrom(address,size_t(2),true)!=2)return false;
  value=uint16_t(Wire.read());value|=uint16_t(Wire.read())<<8;return true;
}
void fail() {
  digitalWrite(RESET_PIN,LOW);running=false;ready=false;
  Serial.println("FAULT: I2C/readback failed. RESET held LOW. Restart simulation.");
}
bool flush() {
  for(uint8_t i=0;i<3;i++)if(!write16(0x20+i,0x14,outputs[i])){fail();return false;}
  return true;
}
void setMotor(uint8_t unit,uint8_t mask) {
  uint8_t bank=unit/4,shift=(unit%4)*4;
  outputs[bank]=(outputs[bank]&~(uint16_t(15)<<shift))|(uint16_t(mask)<<shift);
}
void stopAll() {
  outputs[0]=outputs[1]=outputs[2]=0;
  if(ready)flush();
}
void setup() {
  Serial.begin(115200);
  pinMode(RESET_PIN,OUTPUT_OPEN_DRAIN);digitalWrite(RESET_PIN,LOW);
  Wire.begin(21,22,100000);delayMicroseconds(20);
  digitalWrite(RESET_PIN,HIGH);delay(2);
  for(uint8_t address=0x20;address<=0x23;address++) {
    Wire.beginTransmission(address);
    if(Wire.endTransmission()!=0){fail();return;}
    uint16_t value;
    if(!read16(address,0x0a,value)||value!=0){fail();return;}
    if(!write16(address,0x14,0)||!write16(address,0x00,address==0x23?0x077f:0)) {fail();return;}
    if(!write16(address,0x02,0)||!write16(address,0x0c,address==0x23?0x077f:0)) {fail();return;}
    if(!read16(address,0,value)||value!=(address==0x23?0x077f:0)) {fail();return;}
    if(!read16(address,0x14,value)||value!=0){fail();return;}
    Serial.printf("PASS MCP 0x%02X direction / output latch\n",address);
  }
  ready=true;
  Serial.println("READY: digital logic demo. 10 motors run in parallel; no real torque or voltage model.");
  Serial.println("Commands: s=stop, a=all motors, 0..9=only that motor, r=reverse direction.");
  Serial.println("Hall switches: left=magnet active (LOW), right=inactive. Hall order U0..U9.");
}
void loop() {
  static int direction=1;
  if(!ready){delay(20);return;}
  while(Serial.available()) {
    char c=Serial.read();
    if(c=='s'){running=false;stopAll();Serial.println("STOPPED: all 40 outputs low.");}
    else if(c=='a'){selected=-1;running=true;Serial.println("RUN all 10 motors");}
    else if(c>='0'&&c<='9'){stopAll();selected=c-'0';running=true;Serial.printf("RUN U%d\n",selected);}
    else if(c=='r'){direction=-direction;Serial.println("DIRECTION reversed");}
  }
  uint32_t now=millis();
  if(running && now-lastStep>=4) {
    lastStep=now;phase=(phase+direction+8)%8;
    for(uint8_t u=0;u<10;u++)setMotor(u,(selected<0||selected==u)?PHASES[phase]:0);
    if(!flush())return;
  }
  if(now-lastHall>=20) {
    lastHall=now;uint16_t raw;
    if(!read16(0x23,0x12,raw)){fail();return;}
    uint16_t hall=0;
    for(uint8_t u=0;u<10;u++)if(!(raw&(1u<<HALL_BITS[u])))hall|=1u<<u;
    if(hall!=oldHall) {
      oldHall=hall;Serial.printf("HALL raw=0x%04X active U0..U9=",raw);
      for(uint8_t u=0;u<10;u++)Serial.print((hall>>u)&1);
      Serial.println();
    }
  }
  delay(1);
}
