// SPDX-License-Identifier: GPL-3.0-only
// ESP32 modular adaptation of David Königsmann's split-flap project.
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>
#include "board_config.h"
#include "characters.h"
#include "mcp_bus.h"
#include "motion.h"

using namespace splitflap;

class WireTransport {
 public:
  bool write16(uint8_t a,uint8_t r,uint16_t v) {
    Wire.beginTransmission(a);Wire.write(r);Wire.write(uint8_t(v));Wire.write(uint8_t(v>>8));
    return Wire.endTransmission()==0;
  }
  bool read8(uint8_t a,uint8_t r,uint8_t& v) {
    Wire.beginTransmission(a);Wire.write(r);
    if(Wire.endTransmission(false)!=0)return false;
    if(Wire.requestFrom(a,size_t(1),true)!=1)return false;
    v=Wire.read();return true;
  }
  bool read16(uint8_t a,uint8_t r,uint16_t& v) {
    Wire.beginTransmission(a);Wire.write(r);
    if(Wire.endTransmission(false)!=0)return false;
    if(Wire.requestFrom(a,size_t(2),true)!=2)return false;
    v=uint16_t(Wire.read());v|=uint16_t(Wire.read())<<8;return true;
  }
};
WireTransport transport;
McpBus<WireTransport> expanders(transport);
MotionController motion;
Preferences settings;
bool settingsAvailable=false, initialized=false, armed=false, noUmlauts=false;
uint8_t enabledCount=0;
uint16_t hallMask=0, activeHighMask=0;
uint32_t lastHallUs=0,lastAuditUs=0,lastLoopUs=0;
char commandBuffer[160];size_t commandLength=0;bool discardLine=false;

// All fields are fixed-width. Enabled motors and arming are deliberately never saved.
struct StoredConfig {
  uint32_t version;
  uint32_t revolution[10],offset[10],interval[10];
  int32_t direction[10];
  uint32_t polarity;
  uint32_t noUmlauts;
};
constexpr uint32_t kStorageVersion=1;

void faultStop(const char* reason) {
  // Independent reset helps even if I2C is broken, but is NOT a safety-rated cutoff.
  digitalWrite(board::kReset,LOW);
  motion.faultAll(FaultCode::OutputFault);armed=false;initialized=false;
  Serial.print("FAULT ");Serial.println(reason);
  Serial.println("CUT MOTOR 12V. Inspect wiring; INIT POWER_OFF is required.");
}

bool initializeBus() {
  armed=false;motion.stop();
  digitalWrite(board::kReset,LOW);delayMicroseconds(10);
  digitalWrite(board::kReset,HIGH);delay(1);
  initialized=expanders.begin();
  if(initialized)initialized=expanders.readHalls(hallMask,activeHighMask);
  if(!initialized){faultStop("I2C_INIT");return false;}
  lastHallUs=lastAuditUs=lastLoopUs=micros();
  return true;
}

void loadSettings() {
  if(!settingsAvailable || settings.getBytesLength("cal")!=sizeof(StoredConfig))return;
  StoredConfig stored{};
  if(settings.getBytes("cal",&stored,sizeof(stored))!=sizeof(stored) ||
     stored.version!=kStorageVersion || stored.polarity>board::kAllUnits || stored.noUmlauts>1)return;
  UnitConfig configs[10];
  for(uint8_t i=0;i<10;++i){
    configs[i].stepsPerRevolution=stored.revolution[i];configs[i].homeOffsetSteps=stored.offset[i];
    configs[i].halfStepIntervalUs=stored.interval[i];
    if(stored.direction[i]!=1 && stored.direction[i]!=-1)return;
    configs[i].phaseDirection=int8_t(stored.direction[i]);
    if(!MotionController::validConfig(configs[i]))return;
  }
  for(uint8_t i=0;i<10;++i)motion.configure(i,configs[i]);
  activeHighMask=uint16_t(stored.polarity);noUmlauts=stored.noUmlauts;
}

bool saveSettings() {
  if(!settingsAvailable || motion.busy())return false;
  StoredConfig stored{};stored.version=kStorageVersion;
  for(uint8_t i=0;i<10;++i){const auto& c=motion.config(i);
    stored.revolution[i]=c.stepsPerRevolution;stored.offset[i]=c.homeOffsetSteps;
    stored.interval[i]=c.halfStepIntervalUs;stored.direction[i]=c.phaseDirection;}
  stored.polarity=activeHighMask;stored.noUmlauts=noUmlauts;
  return settings.putBytes("cal",&stored,sizeof(stored))==sizeof(stored);
}

bool integer(const char* text,long min,long max,long& value) {
  if(!text || !*text)return false;
  const char* p=text;if(*p=='-')++p;
  if(!*p)return false;
  while(*p){if(*p<'0'||*p>'9')return false;++p;}
  char* end=nullptr;value=strtol(text,&end,10);
  return *end==0 && value>=min && value<=max;
}

void status() {
  if(motion.busy()) { Serial.println("BUSY; STOP is available; detailed STATUS when idle");return; }
  Serial.printf("bus=%s armed=%u enabled=%u limit=%u hall=0x%03x charset=%s nvs=%u\n",
      initialized?"ready":"fault/off",armed,enabledCount,motion.maxConcurrent(),hallMask,
      noUmlauts?"NOUM":"UMLAUT",settingsAvailable);
  for(uint8_t i=0;i<10;++i){const auto& c=motion.config(i);
    Serial.printf("%u %s fault=%s homed=%u slot=%u rev=%lu offset=%lu dir=%d us=%lu hall=%s\n",
      i,MotionController::stateName(motion.state(i)),MotionController::faultName(motion.fault(i)),
      motion.homed(i),motion.currentIndex(i),(unsigned long)c.stepsPerRevolution,
      (unsigned long)c.homeOffsetSteps,c.phaseDirection,(unsigned long)c.halfStepIntervalUs,
      activeHighMask&(1u<<i)?"HIGH":"LOW");
  }
}

void executeCommand(char* line) {
  if(strcmp(line,"STOP")==0){motion.stop();armed=false;uint8_t off[10]={};
    if(initialized && !expanders.flush(off)){faultStop("I2C_STOP");return;}
    Serial.println("OK stopped/disarmed; position invalid; HOME required");return;}
  if(strcmp(line,"STATUS")==0){status();return;}
  if(motion.busy()){Serial.println("ERR busy; only STOP/STATUS accepted");return;}
  if(strcmp(line,"INIT POWER_OFF")==0){
    Serial.println(initializeBus()?"OK bus initialized; verify safe power before ARM":"ERR initialization");return;}
  if(strcmp(line,"ARM")==0){
    if(!initialized||enabledCount==0){Serial.println("ERR INIT and ENABLE required");return;}
    if(!expanders.audit()){faultStop("I2C_AUDIT");return;}
    armed=true;Serial.println("OK armed; no automatic motion");return;}
  if(strcmp(line,"SAVE")==0){Serial.println(saveSettings()?"OK calibration saved":"ERR save");return;}
  if(strcmp(line,"SCAN POWER_OFF")==0){
    motion.stop();armed=false;initialized=false;
    digitalWrite(board::kReset,LOW);delayMicroseconds(10);
    digitalWrite(board::kReset,HIGH);delay(1);
    for(uint8_t a=0x20;a<=0x27;++a){Wire.beginTransmission(a);if(Wire.endTransmission()==0)Serial.printf("0x%02x\n",a);}
    digitalWrite(board::kReset,LOW);
    return;
  }
  if(strcmp(line,"HELP")==0){
    Serial.println("INIT POWER_OFF | SCAN POWER_OFF | ENABLE 0..10 | LIMIT 1..10 | ARM | HOME ALL|unit");
    Serial.println("MOVE unit slot | TEXT text | CAL unit rev offset dir us | POLARITY unit LOW|HIGH");
    Serial.println("CHARSET UMLAUT|NOUM | SAVE | STATUS | STOP");return;
  }
  if(strncmp(line,"TEXT ",5)==0){
    uint8_t indices[10];
    if(!armed || !enabledCount || !parseText(line+5,indices,enabledCount,noUmlauts)){
      Serial.println("ERR text/arming; uppercase, matching charset, <= enabled count");return;}
    for(uint8_t i=0;i<enabledCount;++i)if(!motion.homed(i)){Serial.println("ERR HOME all enabled units first");return;}
    for(uint8_t i=0;i<enabledCount;++i)if(!motion.setTarget(i,indices[i])){faultStop("TARGET_REJECTED");return;}
    Serial.println("OK queued");return;
  }
  char* argv[8]={};uint8_t argc=0;char* state=nullptr;
  for(char* t=strtok_r(line," ",&state);t;t=strtok_r(nullptr," ",&state)){
    if(argc==8){Serial.println("ERR too many arguments");return;}argv[argc++]=t;}
  if(argc==0)return;
  long a=0,b=0,c=0,d=0,e=0;
  if(strcmp(argv[0],"ENABLE")==0 && argc==2 && integer(argv[1],0,10,a)){
    motion.stop();armed=false;
    for(uint8_t i=0;i<10;++i)motion.setEnabled(i,i<a);
    enabledCount=uint8_t(a);Serial.println("OK enabled prefix; ARM then HOME");return;
  }
  if(strcmp(argv[0],"LIMIT")==0 && argc==2 && integer(argv[1],1,10,a)){
    motion.setMaxConcurrent(uint8_t(a));Serial.println("OK concurrency set; verify power before increasing");return;
  }
  if(strcmp(argv[0],"HOME")==0 && argc==2 && armed){
    uint16_t mask=0;
    if(strcmp(argv[1],"ALL")==0)mask=uint16_t((1u<<enabledCount)-1u);
    else if(integer(argv[1],0,9,a))mask=uint16_t(1u<<a);
    Serial.println(motion.beginHome(mask)?"OK homing queued":"ERR home request");return;
  }
  if(strcmp(argv[0],"MOVE")==0 && argc==3 && armed && integer(argv[1],0,9,a) && integer(argv[2],0,44,b)){
    Serial.println(motion.setTarget(uint8_t(a),uint8_t(b))?"OK move queued":"ERR home/enabled required");return;
  }
  if(strcmp(argv[0],"CAL")==0 && argc==6 && integer(argv[1],0,9,a) &&
      integer(argv[2],1000,20000,b) && integer(argv[3],0,b-1,c) &&
      integer(argv[4],-1,1,d) && d!=0 && integer(argv[5],3000,30000,e)){
    UnitConfig cfg=motion.config(uint8_t(a));cfg.stepsPerRevolution=uint32_t(b);
    cfg.homeOffsetSteps=uint32_t(c);cfg.phaseDirection=int8_t(d);cfg.halfStepIntervalUs=uint32_t(e);
    const bool ok=motion.configure(uint8_t(a),cfg);armed=false;
    Serial.println(ok?"OK calibration changed; ARM/HOME; SAVE to persist":"ERR calibration");return;
  }
  if(strcmp(argv[0],"POLARITY")==0 && argc==3 && integer(argv[1],0,9,a) &&
     (strcmp(argv[2],"LOW")==0||strcmp(argv[2],"HIGH")==0)){
    if(strcmp(argv[2],"HIGH")==0)activeHighMask|=uint16_t(1u<<a);else activeHighMask&=uint16_t(~(1u<<a));
    motion.stop();armed=false;Serial.println("OK polarity; ARM/HOME; SAVE to persist");return;
  }
  if(strcmp(argv[0],"CHARSET")==0 && argc==2 && (strcmp(argv[1],"NOUM")==0||strcmp(argv[1],"UMLAUT")==0)){
    noUmlauts=strcmp(argv[1],"NOUM")==0;Serial.println("OK charset; SAVE to persist");return;
  }
  Serial.println("ERR command/arguments/arming; HELP");
}

void pollSerial() {
  // Bounded ingress: never wait for a line or allow an unbounded String.
  for(uint8_t budget=0;budget<32 && Serial.available();++budget){
    const char c=char(Serial.read());if(c=='\r')continue;
    if(c=='\n'){
      if(discardLine)Serial.println("ERR line too long; discarded");
      else{commandBuffer[commandLength]=0;executeCommand(commandBuffer);}
      commandLength=0;discardLine=false;break;
    }
    if(discardLine)continue;
    if(commandLength+1>=sizeof(commandBuffer)){discardLine=true;commandLength=0;continue;}
    commandBuffer[commandLength++]=c;
  }
}

void setup() {
  // Apply reset as early as software permits. 12V must be off for commissioning/reset.
  digitalWrite(board::kReset,LOW);pinMode(board::kReset,OUTPUT_OPEN_DRAIN);
  Serial.begin(115200);Wire.begin(board::kSda,board::kScl,board::kI2cHz);Wire.setTimeOut(5);
  settingsAvailable=settings.begin("splitflap",false);loadSettings();
  // Deliberately do not initialize/arm/move on boot. Explicit power-off acknowledgement.
  lastLoopUs=micros();
  Serial.println("ESP32 split-flap: motor 12V OFF; verify wiring; INIT POWER_OFF then HELP.");
}

void loop() {
  uint32_t now=micros();
  if(motion.busy() && uint32_t(now-lastLoopUs)>board::kMainLoopStallUs)faultStop("LOOP_STALL");
  lastLoopUs=now;
  if(initialized){
    bool freshHall=false;
    if(uint32_t(now-lastAuditUs)>=board::kBusAuditUs){
      lastAuditUs=now;if(!expanders.audit())faultStop("I2C_AUDIT");
    }
    now=micros(); // Auditing may consume several ms; do not use its pre-I2C timestamp.
    if(initialized && uint32_t(now-lastHallUs)>=board::kHallPollUs){
      if(!expanders.readHalls(hallMask,activeHighMask))faultStop("I2C_HALL");
      else {lastHallUs=micros();freshHall=true;}
    }
    if(initialized && freshHall){
      // A tick consumes a successful fresh sample, never a cached debounce value.
      // Use post-I2C time, never advance several phases to catch up after latency.
      motion.tick(micros(),hallMask);
      for(uint8_t i=0;i<enabledCount;++i)if(motion.state(i)==State::Fault){
        digitalWrite(board::kReset,LOW); // Cut MCP drive before potentially blocking UART diagnostics.
        Serial.printf("unit=%u cause=%s\n",i,MotionController::faultName(motion.fault(i)));
        faultStop("MOTION_HOME");break;}
      if(initialized){uint8_t coils[10];for(uint8_t i=0;i<10;++i)coils[i]=motion.coilMask(i);
        if(!expanders.flush(coils))faultStop("I2C_OUTPUT");}
    }
  }
  pollSerial();
  yield();
}
