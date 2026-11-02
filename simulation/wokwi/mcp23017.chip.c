// SPDX-License-Identifier: GPL-3.0-only
// Original digital subset model, written for this project. Not a full MCP23017.
#include "wokwi-api.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  pin_t gpio[16], address[3], reset, vdd;
  uint8_t reg[0x16], pointer;
  bool expect_register;
} chip_state_t;

static void apply_outputs(chip_state_t *s) {
  for (int i=0;i<16;i++) {
    const int port=i/8, bit=i%8;
    if (s->reg[port] & (1u<<bit))
      pin_mode(s->gpio[i], (s->reg[0x0c+port] & (1u<<bit)) ? INPUT_PULLUP : INPUT);
    else {
      pin_mode(s->gpio[i], OUTPUT);
      pin_write(s->gpio[i], (s->reg[0x14+port] >> bit)&1);
    }
  }
}
static void reset_device(chip_state_t *s) {
  memset(s->reg,0,sizeof(s->reg));
  s->reg[0]=s->reg[1]=0xff;
  s->pointer=0; s->expect_register=true;
  apply_outputs(s);
}
static void reset_change(void *data,pin_t pin,uint32_t value) {
  (void)pin;
  if (!value) reset_device(data);
}
static bool connect_device(void *data,uint32_t address,bool read) {
  chip_state_t *s=data;
  uint8_t actual=0x20;
  for(int i=0;i<3;i++) actual|=pin_read(s->address[i])<<i;
  if (!pin_read(s->reset) || !pin_read(s->vdd) || address!=actual) return false;
  if (!read) s->expect_register=true;
  return true;
}
static void advance(chip_state_t *s) {
  if (!(s->reg[0x0a]&0x20)) s->pointer=(s->pointer+1)%0x16;
}
static uint8_t read_device(void *data) {
  chip_state_t *s=data;
  uint8_t r=s->pointer, value=0;
  if (r>=0x16) return 0;
  if (r==0x12 || r==0x13) {
    int port=r-0x12;
    for(int bit=0;bit<8;bit++) value|=pin_read(s->gpio[port*8+bit])<<bit;
    value^=s->reg[2+port]&s->reg[port]; // IPOL applies only to inputs.
  } else value=s->reg[r];
  advance(s); return value;
}
static bool write_device(void *data,uint8_t value) {
  chip_state_t *s=data;
  if (s->expect_register) { s->pointer=value; s->expect_register=false; return value<0x16; }
  uint8_t r=s->pointer;
  if(r>=0x16) return false;
  if(r==0x0a || r==0x0b) {
    // Fail visibly rather than pretending to implement BANK=1 or interrupts.
    if(value&0x80) { printf("MCP model: BANK=1 is unsupported\n"); return false; }
    s->reg[0x0a]=s->reg[0x0b]=value;
  } else if(r==0 || r==1 || r==2 || r==3 || r==0x0c || r==0x0d) {
    s->reg[r]=value;
  } else if(r==0x12 || r==0x13) s->reg[r+2]=value;
  else if(r==0x14 || r==0x15) s->reg[r]=value;
  else { printf("MCP model: unsupported register write 0x%02x\n",r); return false; }
  apply_outputs(s); advance(s); return true;
}
void chip_init(void) {
  chip_state_t *s=calloc(1,sizeof(*s));
  char name[6];
  for(int i=0;i<16;i++) { snprintf(name,sizeof(name),"GP%c%d",i<8?'A':'B',i%8); s->gpio[i]=pin_init(name,INPUT); }
  for(int i=0;i<3;i++) { snprintf(name,sizeof(name),"A%d",i); s->address[i]=pin_init(name,INPUT_PULLDOWN); }
  s->vdd=pin_init("VDD",INPUT);
  pin_init("VSS",INPUT);
  pin_init("INTA",INPUT); pin_init("INTB",INPUT); // Not modeled, unused.
  s->reset=pin_init("RESET",INPUT_PULLUP);
  pin_watch_config_t watch={.edge=BOTH,.pin_change=reset_change,.user_data=s};
  pin_watch(s->reset,&watch);
  reset_device(s);
  i2c_config_t bus={.address=0,.scl=pin_init("SCL",INPUT_PULLUP),.sda=pin_init("SDA",INPUT_PULLUP),
    .connect=connect_device,.read=read_device,.write=write_device,.user_data=s};
  i2c_init(&bus);
}
